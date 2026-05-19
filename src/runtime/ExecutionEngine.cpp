#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "Opcodes.h"
#include "debugger/DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "protoCore.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

const proto::ProtoObject*
ExecutionEngine::run(proto::ProtoContext* ctx,
                     const BytecodeModule& m,
                     const proto::ProtoObject* self) {
    return runWithArgs(ctx, m, self, nullptr, 0);
}

const proto::ProtoObject*
ExecutionEngine::runWithArgs(proto::ProtoContext* ctx,
                             const BytecodeModule& m,
                             const proto::ProtoObject* /*self*/,
                             const proto::ProtoObject* const* args,
                             int argc,
                             const proto::ProtoObject* capturedDict) {
    const auto& bytes = m.bytes();
    std::size_t pc = 0;

    // F3: captured-locals dictionary. At the top level the dict is
    // pre-allocated by STRuntime::runTopLevel; for block invocations the
    // outer's dict is threaded through via invokeBlock. nullptr means the
    // module declares no captured names.
    const proto::ProtoObject* captured = capturedDict;

    // F2: a plain operand stack. The "no std::vector" rule applies fully
    // when the actor model arrives in F6, which is out of scope for this task.
    std::vector<const proto::ProtoObject*> stack;
    stack.reserve(64);

    // F2: per-method locals as a small std::vector. Replaced with
    // ProtoSparseList in Task 50 when actors arrive (out of scope for F1+F2).
    // Pre-populate slots 0..argc-1 from incoming args (Task 44: block invocation).
    std::vector<const proto::ProtoObject*> locals(args, args + argc);
    locals.reserve(std::max<size_t>(16, locals.size() + 8));

    auto ensureLocal = [&](uint8_t slot) {
        if (slot >= locals.size())
            locals.resize(static_cast<size_t>(slot) + 1, PROTO_NONE);
    };

    // Outer loop: lets us resume after a DebuggerHalt is caught and the
    // user steps/conts out of the session. Without this wrapper the catch
    // handler would return early and the remainder of the module would be
    // skipped.
    while (true) {
    try {
    while (pc + 1 < bytes.size()) {
        // F2 single-step support: if the debugger is attached and in a
        // non-Free mode, enter the session BEFORE each instruction. The
        // session may flip mode back to Free (e.g. user typed 'c'), in
        // which case subsequent instructions run at full speed.
        // Calling enterSession directly (rather than throwing Halt) avoids
        // the cost of unwinding the C++ stack on every step.
        auto dbgMode = rt_.debugger().mode();
        if (rt_.debugger().attached() && dbgMode != DebuggerRuntime::Mode::Free) {
            DebugFrame frame;
            frame.module = &m;
            frame.pc = pc;
            frame.stack.assign(stack.begin(), stack.end());
            frame.locals.assign(locals.begin(), locals.end());
            rt_.debugger().enterSession(rt_, std::move(frame), "step");
            // Session may have updated mode (e.g. user typed 'c'); fall
            // through to dispatch the next instruction.
        }

        // F2 location breakpoint: halt BEFORE executing the instruction at pc.
        if (rt_.debugger().attached() && rt_.debugger().breakpoints().isSet(&m, pc)) {
            DebugFrame frame;
            frame.module = &m;
            frame.pc = pc;
            frame.stack.assign(stack.begin(), stack.end());
            frame.locals.assign(locals.begin(), locals.end());
            rt_.debugger().enterSession(rt_, std::move(frame), "breakpoint");
        }

        const Op op = static_cast<Op>(bytes[pc]);
        const uint8_t arg = bytes[pc + 1];
        pc += kInstrSize;

        switch (op) {
            case Op::NOP:
                break;
            case Op::PUSH_NIL:
                stack.push_back(PROTO_NONE);
                break;
            case Op::PUSH_TRUE:
                stack.push_back(PROTO_TRUE);
                break;
            case Op::PUSH_FALSE:
                stack.push_back(PROTO_FALSE);
                break;
            case Op::PUSH_CONST:
                stack.push_back(rt_.materialize(m, arg));
                break;
            case Op::DUP:
                stack.push_back(stack.back());
                break;
            case Op::POP:
                stack.pop_back();
                break;
            case Op::PUSH_LOCAL:
                ensureLocal(arg);
                stack.push_back(locals[arg]);
                break;
            case Op::STORE_LOCAL: {
                ensureLocal(arg);
                if (stack.empty())
                    throw std::runtime_error("STORE_LOCAL with empty stack");
                locals[arg] = stack.back();
                stack.pop_back();
                break;
            }
            case Op::RETURN:
            case Op::RETURN_TOP: {
                // F4-U4: Op::RETURN is emitted by an explicit `^expr` in a
                // method or block body; Op::RETURN_TOP is emitted by the
                // compiler as the implicit trailer for a top-level module
                // and for blocks. In the current engine both pop the top of
                // the operand stack and return it to the caller of this
                // runWithArgs invocation. (True non-local-return semantics —
                // `^` from inside a nested block unwinds to its enclosing
                // method — is out of scope for F4-U4 and will be revisited
                // when blocks-inside-methods become reachable.)
                const proto::ProtoObject* r =
                    stack.empty() ? PROTO_NONE : stack.back();
                return r;
            }
            case Op::PUSH_BLOCK: {
                // arg = block index inside m.blocks(). Create a fresh BlockClosure
                // (a mutable child of the Block prototype) that carries an opaque
                // pointer to the sub-module under attribute "__bc_ptr__".
                auto* blkProto = rt_.bootstrap().blockProto;
                auto* block = blkProto->newChild(ctx, /*isMutable=*/true);
                static const proto::ProtoString* bcKey =
                    ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                static const proto::ProtoString* capKey =
                    ctx->fromUTF8String("__captured__")->asString(ctx);
                auto* bcPtrObj = ctx->fromLong(
                    reinterpret_cast<long long>(&m.block(arg)));
                block->setAttribute(ctx, bcKey, bcPtrObj);
                // F3-C5: stash the current frame's captured dict so the block can
                // resolve free variables back to the outer scope at invocation time.
                block->setAttribute(
                    ctx, capKey,
                    captured ? captured : PROTO_NONE);
                stack.push_back(block);
                break;
            }
            case Op::SEND_UNARY:
            case Op::SEND_BINARY:
            case Op::SEND_KEYWORD: {
                // pop N args (0 for unary, 1 for binary, count from selector for keyword)
                int argc = (op == Op::SEND_UNARY)  ? 0
                         : (op == Op::SEND_BINARY) ? 1
                         : /* keyword */ 0;
                const std::string& selStr = m.constSymbol(arg);
                if (op == Op::SEND_KEYWORD) for (char c : selStr) if (c == ':') ++argc;

                if (static_cast<int>(stack.size()) < argc + 1)
                    throw std::runtime_error("SEND with insufficient stack");
                if (argc > 8) throw std::runtime_error("F2 limit: <=8 args per send");
                const proto::ProtoObject* args[8];
                for (int i = argc - 1; i >= 0; --i) {
                    args[i] = stack.back();
                    stack.pop_back();
                }
                const proto::ProtoObject* recv = stack.back(); stack.pop_back();

                auto* selSym = ctx->fromUTF8String(selStr.c_str())->asString(ctx);
                // Lookup walks from the receiver itself first, then up the
                // prototype chain.  Starting at the receiver matters for
                // "class as receiver" patterns (F4): a class prototype
                // carries its instance methods as own-attributes, and
                // messages sent to the class itself must resolve to those
                // methods.  For ordinary instance receivers, getAttribute
                // walks past the (typically empty) own-attribute slot to the
                // prototype where the method is bound.
                //
                // Note: getAttribute on a tagged immediate (SmallInteger,
                // Boolean, ...) correctly delegates to its protoCore
                // prototype slot (smallIntegerPrototype etc.), which our
                // bootstrap has already re-pointed at the protoST
                // prototypes (see Bootstrap.cpp).
                auto* attr = recv->getAttribute(ctx, selSym);
                if (!attr || attr == PROTO_NONE) {
                    throw std::runtime_error("doesNotUnderstand: " + selStr);
                }
                // F4-U4: detect user method (Block-shaped wrapper carrying
                // __bc_ptr__). User methods are installed by Compiler-emitted
                // __installMethod:as: and are not tagged primitive markers.
                // We probe for __bc_ptr__ first; if absent, fall through to
                // the legacy primitive-marker (tagged SmallInteger with bit
                // 62 set) dispatch.
                static const proto::ProtoString* bcKey =
                    ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                static const proto::ProtoString* capKey =
                    ctx->fromUTF8String("__captured__")->asString(ctx);
                auto* bcPtrObj = attr->getAttribute(ctx, bcKey);
                if (bcPtrObj && bcPtrObj != PROTO_NONE) {
                    // User method: invoke the sub-module with recv prepended
                    // to the args (recv lands in slot 0 as `self`).
                    const protoST::BytecodeModule* sub =
                        reinterpret_cast<const protoST::BytecodeModule*>(
                            bcPtrObj->asLong(ctx));
                    if (sub->argCount() != argc + 1) {
                        throw std::runtime_error(
                            "method " + selStr + " expects " +
                            std::to_string(sub->argCount() - 1) +
                            " args, got " + std::to_string(argc));
                    }
                    auto* capDict = attr->getAttribute(ctx, capKey);
                    if (capDict == PROTO_NONE) capDict = nullptr;
                    std::vector<const proto::ProtoObject*> methodArgs;
                    methodArgs.reserve(static_cast<size_t>(argc) + 1);
                    methodArgs.push_back(recv);
                    for (int i = 0; i < argc; ++i) methodArgs.push_back(args[i]);
                    ExecutionEngine subEng(rt_);
                    auto* result = subEng.runWithArgs(
                        ctx, *sub, /*self=*/recv,
                        methodArgs.data(),
                        static_cast<int>(methodArgs.size()),
                        capDict);
                    stack.push_back(result ? result : PROTO_NONE);
                    break;
                }
                long long marker = attr->asLong(ctx);
                if (!(marker & (1LL << 62))) {
                    throw std::runtime_error("non-primitive method in F2 (F3 work)");
                }
                int primIdx = static_cast<int>(marker & ((1LL << 62) - 1));
                auto fn = rt_.registry().at(primIdx);
                auto* result = fn(rt_, ctx, recv, args, argc);
                stack.push_back(result ? result : PROTO_NONE);
                break;
            }
            case Op::JUMP:          pc += static_cast<size_t>(arg) * kInstrSize; break;
            case Op::JUMP_IF_TRUE: {
                auto* v = stack.back(); stack.pop_back();
                if (v == PROTO_TRUE) pc += static_cast<size_t>(arg) * kInstrSize;
                break;
            }
            case Op::JUMP_IF_FALSE: {
                auto* v = stack.back(); stack.pop_back();
                if (v == PROTO_FALSE) pc += static_cast<size_t>(arg) * kInstrSize;
                break;
            }
            case Op::PUSH_CAPTURED: {
                // arg = constant pool index of the (interned) symbol name.
                const std::string& nameStr = m.constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                const proto::ProtoObject* val = captured
                    ? captured->getAttribute(ctx, sym)
                    : nullptr;
                stack.push_back(val ? val : PROTO_NONE);
                break;
            }
            case Op::STORE_CAPTURED: {
                if (stack.empty())
                    throw std::runtime_error("STORE_CAPTURED with empty stack");
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (!captured)
                    throw std::runtime_error("STORE_CAPTURED without captured dict");
                const std::string& nameStr = m.constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                const_cast<proto::ProtoObject*>(captured)->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_GLOBAL: {
                // arg = constant pool index of the (interned) global name.
                const std::string& nameStr = m.constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                auto* g = rt_.globals();
                auto* val = g ? g->getAttribute(ctx, sym) : nullptr;
                if (!val || val == PROTO_NONE)
                    throw std::runtime_error("undefined global: " + nameStr);
                stack.push_back(val);
                break;
            }
            case Op::STORE_GLOBAL: {
                if (stack.empty())
                    throw std::runtime_error("STORE_GLOBAL with empty stack");
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                const std::string& nameStr = m.constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                auto* g = rt_.globals();
                if (!g) throw std::runtime_error("globals not initialized");
                g->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_INSTVAR:
            case Op::STORE_INSTVAR:
                throw std::runtime_error("PUSH_INSTVAR / STORE_INSTVAR not yet implemented (F4-U4)");
            default:
                throw std::runtime_error(
                    "ExecutionEngine: unimplemented opcode at pc=" +
                    std::to_string(pc - kInstrSize));
        }
    }
    return PROTO_NONE;
    } catch (DebuggerHalt& h) {
        DebugFrame frame;
        frame.module = &m;
        frame.pc = pc;
        frame.stack.assign(stack.begin(), stack.end());
        frame.locals.assign(locals.begin(), locals.end());
        rt_.debugger().enterSession(rt_, std::move(frame), h.reason());
        // After the session resumes (user typed c/s/n/f), fall back through
        // the outer while(true) and continue executing from the current pc.
        // The halt primitive pushes PROTO_NONE as its return value into the
        // caller's stack via the SEND_UNARY handler, but throwing aborted
        // that. Push PROTO_NONE now so the operand stack matches what the
        // compiler expects after a unary send.
        stack.push_back(PROTO_NONE);
        continue;
    }
    } // outer while(true)
}

} // namespace protoST
