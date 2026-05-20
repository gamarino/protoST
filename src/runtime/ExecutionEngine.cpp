// ExecutionEngine — non-recursive Smalltalk bytecode dispatcher.
//
// COOPERATIVE YIELD LIMITATION (F6 v3 v1):
//   Block invocation via direct `value` / `value:` / `value:value:` / ...
//   SEND is non-recursive (F6 v3 A2: handled inline in this engine by
//   pushing a Frame) and therefore yieldable from inside the block body.
//
//   Block invocation via primitives that take the block as an argument
//   and evaluate it internally — `ifTrue:`, `ifFalse:`, `ifTrue:ifFalse:`,
//   `whileTrue:`, `thenDo:`, `catch:` — currently goes through
//   block_prims.cpp::invokeBlock, which spins up a fresh recursive
//   ExecutionEngine on the C++ stack. A `Future>>wait` inside such a
//   primitive-evaluated block will block its worker thread the F6 v2 way
//   and is NOT cooperatively yieldable.
//
//   The common digital-twin patterns (sequential statements with `wait`
//   and a direct `[ ... ] value`) are fully cooperative; loops and
//   conditionals containing `wait` are not. Lifting those primitives into
//   the engine is future work.
#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "Opcodes.h"
#include "ActorLock.h"
#include "debugger/DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "protoCore.h"

#include <algorithm>
#include <mutex>
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
                             const proto::ProtoObject* selfObj,
                             const proto::ProtoObject* const* args,
                             int argc,
                             const proto::ProtoObject* capturedDict) {
    // F6 v3 A: build the initial Frame and enter the single dispatch loop.
    // Previously the operand stack / locals / pc lived as C++ stack variables
    // and a recursive runWithArgs call ran a sub-engine; now everything is
    // explicit in frames_ and user-method SENDs push frames instead of
    // recursing.
    frames_.clear();
    // Pre-reserve to keep Frame references stable across moderate push depth.
    // 64 is a typical recursive Smalltalk method nesting; we never read &back
    // across a push without re-acquiring, but the reservation avoids excess
    // reallocation churn anyway.
    frames_.reserve(64);

    Frame top;
    top.m = &m;
    top.pc = 0;
    top.opStack.reserve(64);
    // Pre-populate slots 0..argc-1 from incoming args (Task 44: block
    // invocation and user-method dispatch both arrive here with `args`
    // already including `self` in slot 0 when applicable).
    top.locals.assign(args, args + argc);
    top.locals.reserve(std::max<std::size_t>(16, top.locals.size() + 8));
    top.selfObj = selfObj;
    top.capturedDict = capturedDict;
    frames_.push_back(std::move(top));

    return runLoop(ctx);
}

const proto::ProtoObject*
ExecutionEngine::runLoop(proto::ProtoContext* ctx) {
    // Helper to ensure a local slot exists in the current frame. The old
    // engine grew `locals` lazily on first STORE_LOCAL/PUSH_LOCAL; we keep
    // that behaviour for compatibility.
    auto ensureLocal = [](Frame& f, uint8_t slot) {
        if (slot >= f.locals.size())
            f.locals.resize(static_cast<std::size_t>(slot) + 1, PROTO_NONE);
    };

    // Outer loop: lets us resume after a DebuggerHalt is caught and the
    // user steps/conts out of the session. Without this wrapper the catch
    // handler would return early and the remainder of the module would be
    // skipped.
    while (true) {
    try {
    while (!frames_.empty()) {
        Frame& f = frames_.back();
        const auto& bytes = f.m->bytes();
        if (f.pc + 1 >= bytes.size()) {
            // Off the end without an explicit RETURN — treat as RETURN_TOP
            // with PROTO_NONE on an empty stack, matching the legacy
            // engine's fallthrough behaviour.
            const proto::ProtoObject* r =
                f.opStack.empty() ? PROTO_NONE : f.opStack.back();
            frames_.pop_back();
            if (frames_.empty()) return r;
            frames_.back().opStack.push_back(r);
            continue;
        }

        // F2 single-step support: if the debugger is attached and in a
        // non-Free mode, enter the session BEFORE each instruction. The
        // session may flip mode back to Free (e.g. user typed 'c'), in
        // which case subsequent instructions run at full speed.
        // Calling enterSession directly (rather than throwing Halt) avoids
        // the cost of unwinding the C++ stack on every step.
        auto dbgMode = rt_.debugger().mode();
        if (rt_.debugger().attached() && dbgMode != DebuggerRuntime::Mode::Free) {
            DebugFrame dframe;
            dframe.module = f.m;
            dframe.pc = f.pc;
            dframe.stack.assign(f.opStack.begin(), f.opStack.end());
            dframe.locals.assign(f.locals.begin(), f.locals.end());
            rt_.debugger().enterSession(rt_, std::move(dframe), "step");
            // Session may have updated mode (e.g. user typed 'c'); fall
            // through to dispatch the next instruction.
        }

        // F2 location breakpoint: halt BEFORE executing the instruction at pc.
        if (rt_.debugger().attached() && rt_.debugger().breakpoints().isSet(f.m, f.pc)) {
            DebugFrame dframe;
            dframe.module = f.m;
            dframe.pc = f.pc;
            dframe.stack.assign(f.opStack.begin(), f.opStack.end());
            dframe.locals.assign(f.locals.begin(), f.locals.end());
            rt_.debugger().enterSession(rt_, std::move(dframe), "breakpoint");
        }

        const Op op = static_cast<Op>(bytes[f.pc]);
        const uint8_t arg = bytes[f.pc + 1];
        f.pc += kInstrSize;

        switch (op) {
            case Op::NOP:
                break;
            case Op::PUSH_NIL:
                f.opStack.push_back(PROTO_NONE);
                break;
            case Op::PUSH_TRUE:
                f.opStack.push_back(PROTO_TRUE);
                break;
            case Op::PUSH_FALSE:
                f.opStack.push_back(PROTO_FALSE);
                break;
            case Op::PUSH_CONST:
                f.opStack.push_back(rt_.materialize(*f.m, arg));
                break;
            case Op::DUP:
                f.opStack.push_back(f.opStack.back());
                break;
            case Op::POP:
                f.opStack.pop_back();
                break;
            case Op::PUSH_LOCAL:
                ensureLocal(f, arg);
                f.opStack.push_back(f.locals[arg]);
                break;
            case Op::STORE_LOCAL: {
                ensureLocal(f, arg);
                if (f.opStack.empty())
                    throw std::runtime_error("STORE_LOCAL with empty stack");
                f.locals[arg] = f.opStack.back();
                f.opStack.pop_back();
                break;
            }
            case Op::RETURN:
            case Op::RETURN_TOP: {
                // F4-U4: Op::RETURN is emitted by an explicit `^expr` in a
                // method or block body; Op::RETURN_TOP is emitted by the
                // compiler as the implicit trailer for a top-level module
                // and for blocks. Both pop the current frame's top-of-stack
                // and hand it back to the caller frame (or return it to the
                // C++ caller of run/runWithArgs when this was the last
                // frame).
                //
                // F6 v3 A: with the explicit Frame stack, "caller frame" is
                // simply frames_[size-2] after the pop. True non-local-return
                // semantics — `^` from inside a nested block unwinds to its
                // enclosing method — remains out of scope (the current test
                // suite does not exercise it; the legacy recursive engine
                // also did not implement it).
                // TODO(F6 v3 follow-up): support non-local return by walking
                // frames_ back to the enclosing method frame on Op::RETURN.
                const proto::ProtoObject* r =
                    f.opStack.empty() ? PROTO_NONE : f.opStack.back();
                frames_.pop_back();
                if (frames_.empty()) return r;
                frames_.back().opStack.push_back(r);
                // Reference `f` is now dangling — continue the loop so the
                // next iteration re-acquires frames_.back().
                continue;
            }
            case Op::PUSH_BLOCK: {
                // arg = block index inside f.m->blocks(). Create a fresh
                // BlockClosure (a mutable child of the Block prototype) that
                // carries an opaque pointer to the sub-module under
                // attribute "__bc_ptr__".
                auto* blkProto = rt_.bootstrap().blockProto;
                auto* block = blkProto->newChild(ctx, /*isMutable=*/true);
                static const proto::ProtoString* bcKey =
                    ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                static const proto::ProtoString* capKey =
                    ctx->fromUTF8String("__captured__")->asString(ctx);
                auto* bcPtrObj = ctx->fromLong(
                    reinterpret_cast<long long>(&f.m->block(arg)));
                block->setAttribute(ctx, bcKey, bcPtrObj);
                // F3-C5: stash the current frame's captured dict so the
                // block can resolve free variables back to the outer scope
                // at invocation time.
                block->setAttribute(
                    ctx, capKey,
                    f.capturedDict ? f.capturedDict : PROTO_NONE);
                f.opStack.push_back(block);
                break;
            }
            case Op::SEND_UNARY:
            case Op::SEND_BINARY:
            case Op::SEND_KEYWORD: {
                // pop N args (0 for unary, 1 for binary, count from selector
                // for keyword)
                int argcOp = (op == Op::SEND_UNARY)  ? 0
                           : (op == Op::SEND_BINARY) ? 1
                           : /* keyword */ 0;
                const std::string& selStr = f.m->constSymbol(arg);
                if (op == Op::SEND_KEYWORD) for (char c : selStr) if (c == ':') ++argcOp;

                if (static_cast<int>(f.opStack.size()) < argcOp + 1)
                    throw std::runtime_error("SEND with insufficient stack");
                if (argcOp > 8) throw std::runtime_error("F2 limit: <=8 args per send");
                const proto::ProtoObject* sendArgs[8];
                for (int i = argcOp - 1; i >= 0; --i) {
                    sendArgs[i] = f.opStack.back();
                    f.opStack.pop_back();
                }
                const proto::ProtoObject* recv = f.opStack.back(); f.opStack.pop_back();

                auto* selSym = ctx->fromUTF8String(selStr.c_str())->asString(ctx);

                // F6-A4: actor fast-path. If the receiver is an actor (i.e.
                // carries a __wrapped__ attribute installed by Object>>asActor),
                // we bypass the synchronous dispatch entirely. The send is
                // converted into a Message envelope, enqueued on the actor's
                // mailbox, the actor is scheduled, and a fresh pending Future
                // is pushed onto the operand stack as the apparent result of
                // the send. The actual method execution happens later when
                // STRuntime::drainOne pulls a message from the mailbox.
                if (rt_.isActor(ctx, recv)) {
                    static const proto::ProtoString* mbKey =
                        ctx->fromUTF8String("__mailbox__")->asString(ctx);
                    static const proto::ProtoString* msgSelKey =
                        ctx->fromUTF8String("__selector__")->asString(ctx);
                    static const proto::ProtoString* msgArgsKey =
                        ctx->fromUTF8String("__args__")->asString(ctx);
                    static const proto::ProtoString* msgFutKey =
                        ctx->fromUTF8String("__future__")->asString(ctx);

                    // Allocate a fresh pending Future.
                    auto* fut = const_cast<proto::ProtoObject*>(rt_.newFuture(ctx));

                    // Build the message envelope (a fresh mutable child of objectProto).
                    auto* msg = const_cast<proto::ProtoObject*>(rt_.bootstrap().objectProto)
                        ->newChild(ctx, /*isMutable=*/true);

                    // Build args ProtoList by FIFO appendLast of each arg.
                    auto* argsList = ctx->newList();
                    for (int i = 0; i < argcOp; ++i) {
                        argsList = argsList->appendLast(ctx, sendArgs[i]);
                    }

                    msg->setAttribute(ctx, msgSelKey, reinterpret_cast<const proto::ProtoObject*>(selSym));
                    msg->setAttribute(ctx, msgArgsKey, argsList->asObject(ctx));
                    msg->setAttribute(ctx, msgFutKey, fut);

                    // F6 v2 T3: hold the per-actor lock across the mailbox
                    // read-modify-write. Without this, the worker thread's
                    // drainOne can pop a message between our getAttribute and
                    // setAttribute calls below; our setAttribute would then
                    // overwrite the popped state and either lose the popped
                    // message (if our append went on top of stale state) or
                    // double-process it.
                    //
                    // We pull the lock pointer outside the {} so we can hold
                    // the guard for the entire RMW and drop it before
                    // schedule() — schedule() takes schedMu internally and
                    // mixing the two acquisition orders elsewhere would risk
                    // deadlock (drainOne acquires schedMu first, then the
                    // actor lock).
                    std::mutex* actorLock = getActorLock(ctx, recv);
                    {
                        std::unique_lock<std::mutex> guard;
                        if (actorLock) guard = std::unique_lock<std::mutex>(*actorLock);

                        // Enqueue (FIFO) into the actor's mailbox.
                        auto* mbObj = recv->getAttribute(ctx, mbKey);
                        auto* mailbox = mbObj ? mbObj->asList(ctx) : ctx->newList();
                        auto* newMailbox = mailbox->appendLast(ctx, msg);
                        const_cast<proto::ProtoObject*>(recv)
                            ->setAttribute(ctx, mbKey, newMailbox->asObject(ctx));
                    }

                    // Schedule the actor for processing and stash the Future
                    // as the apparent result of the send. schedule() acquires
                    // schedMu; doing it OUTSIDE the actor lock keeps the
                    // global acquisition order (schedMu first, actor lock
                    // second) consistent with drainOne.
                    rt_.schedule(recv);
                    f.opStack.push_back(fut);
                    break;
                }

                // F6 v3 A2: direct block invocation fast-path. If the
                // receiver carries `__bc_ptr__` as an OWN attribute (it is a
                // BlockClosure built by PUSH_BLOCK) AND the selector is a
                // `value`-family selector matching its formal-arg count, we
                // push a block Frame onto frames_ and let runLoop continue.
                // This is the equivalent of the user-method push-frame path
                // (F6 v3 A) but specialised for blocks: no self-binding into
                // slot 0, args go into slots 0..argcOp-1, captured dict
                // threaded from the closure's `__captured__` attribute.
                //
                // The legacy path (primitive `prim_Block_value` in
                // block_prims.cpp spinning a fresh recursive
                // ExecutionEngine) remains as a fallback for indirect block
                // invocation (e.g. via `perform:` once that exists, or any
                // dispatch path that does NOT come through SEND_UNARY /
                // SEND_KEYWORD here). It is also still used by
                // `ifTrue:`/`whileTrue:`/`thenDo:` primitives that take a
                // block as an argument and evaluate it internally — those
                // remain non-yieldable. See the limitation block at the top
                // of this file.
                {
                    static const proto::ProtoString* recvBcKey =
                        ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                    static const proto::ProtoString* recvCapKey =
                        ctx->fromUTF8String("__captured__")->asString(ctx);
                    // getAttribute walks the receiver first then the proto
                    // chain. `__bc_ptr__` is set only on BlockClosures (by
                    // PUSH_BLOCK), never on `blockProto` itself, so a hit
                    // here is unambiguous evidence that `recv` is a block.
                    auto* recvBcPtr = recv->getAttribute(ctx, recvBcKey);
                    if (recvBcPtr && recvBcPtr != PROTO_NONE) {
                        // Validate selector matches the value/value:/...
                        // pattern for argcOp formal parameters. argcOp is
                        // already computed from the selector colons above
                        // (or 0 for SEND_UNARY which uses bare `value`).
                        bool isValueFamily = false;
                        if (op == Op::SEND_UNARY) {
                            isValueFamily = (selStr == "value");
                        } else if (op == Op::SEND_KEYWORD) {
                            // selector must be exactly argcOp repetitions
                            // of "value:" — i.e. "value:", "value:value:",
                            // "value:value:value:", ...
                            isValueFamily = true;
                            std::string expected;
                            for (int i = 0; i < argcOp; ++i) expected += "value:";
                            if (selStr != expected) isValueFamily = false;
                        }
                        // SEND_BINARY never matches value-family.
                        if (isValueFamily) {
                            const protoST::BytecodeModule* sub =
                                reinterpret_cast<const protoST::BytecodeModule*>(
                                    recvBcPtr->asLong(ctx));
                            if (sub->argCount() != argcOp) {
                                throw std::runtime_error(
                                    "block arg count mismatch (expected " +
                                    std::to_string(sub->argCount()) +
                                    ", got " + std::to_string(argcOp) + ")");
                            }
                            auto* capDict = recv->getAttribute(ctx, recvCapKey);
                            if (!capDict || capDict == PROTO_NONE) capDict = nullptr;

                            Frame callee;
                            callee.m = sub;
                            callee.pc = 0;
                            callee.opStack.reserve(64);
                            callee.locals.reserve(
                                std::max<std::size_t>(16,
                                    static_cast<std::size_t>(argcOp) + 8));
                            // Blocks bind args into slots 0..argcOp-1 (no
                            // implicit self slot; cf. invokeBlock which
                            // passes self=PROTO_NONE). PUSH_INSTVAR inside
                            // a block reads f.locals[0] which would
                            // therefore be the first arg, not the
                            // enclosing method's self — same pre-existing
                            // limitation as the legacy invokeBlock path.
                            for (int i = 0; i < argcOp; ++i)
                                callee.locals.push_back(sendArgs[i]);
                            callee.selfObj = PROTO_NONE;
                            callee.capturedDict = capDict;
                            frames_.push_back(std::move(callee));
                            // `f` is now invalidated when frames_ grows
                            // its backing storage. Continue the loop so
                            // the next iteration re-acquires
                            // frames_.back() and dispatches the block
                            // from its pc=0.
                            continue;
                        }
                    }
                }

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
                    // User method: F6 v3 A previously recursed via
                    //   ExecutionEngine subEng(rt_);
                    //   subEng.runWithArgs(...)
                    // — i.e. a new C++ stack frame per Smalltalk call.
                    // Now we push a Frame onto our own frames_ instead and
                    // let runLoop pick it up. The callee's eventual
                    // RETURN/RETURN_TOP pops its frame and pushes its
                    // result onto OUR opStack (this very `f.opStack`,
                    // though by then `f` is a dangling reference).
                    const protoST::BytecodeModule* sub =
                        reinterpret_cast<const protoST::BytecodeModule*>(
                            bcPtrObj->asLong(ctx));
                    if (sub->argCount() != argcOp + 1) {
                        throw std::runtime_error(
                            "method " + selStr + " expects " +
                            std::to_string(sub->argCount() - 1) +
                            " args, got " + std::to_string(argcOp));
                    }
                    auto* capDict = attr->getAttribute(ctx, capKey);
                    if (capDict == PROTO_NONE) capDict = nullptr;

                    Frame callee;
                    callee.m = sub;
                    callee.pc = 0;
                    callee.opStack.reserve(64);
                    callee.locals.reserve(
                        std::max<std::size_t>(16,
                            static_cast<std::size_t>(argcOp) + 1 + 8));
                    // Slot 0 = recv (self), slots 1..argcOp = call args.
                    callee.locals.push_back(recv);
                    for (int i = 0; i < argcOp; ++i)
                        callee.locals.push_back(sendArgs[i]);
                    callee.selfObj = recv;
                    callee.capturedDict = capDict;
                    frames_.push_back(std::move(callee));
                    // `f` is now invalidated by the vector growth (when it
                    // reallocates). Continue the loop so the next iteration
                    // re-acquires frames_.back() and dispatches the callee
                    // from its pc=0.
                    continue;
                }
                // F5-M3: member-access semantics. If the attribute is not a
                // method (no __bc_ptr__) and is not a primitive marker (a
                // tagged SmallInteger with bit 62 set), then a unary send is
                // treated as a property read — the attribute is itself the
                // result. This is what lets `m Greeter` resolve the class
                // stored on a module wrapper (or any plain value attribute).
                // For non-unary sends, treating a value attribute as a method
                // is still an error.
                if (!attr->isInteger(ctx)) {
                    if (argcOp == 0) {
                        f.opStack.push_back(attr);
                        break;
                    }
                    throw std::runtime_error(
                        "non-primitive method in F2 (F3 work): " + selStr);
                }
                long long marker = attr->asLong(ctx);
                if (!(marker & (1LL << 62))) {
                    // Plain integer value stored as an attribute — same
                    // member-access rule as above.
                    if (argcOp == 0) {
                        f.opStack.push_back(attr);
                        break;
                    }
                    throw std::runtime_error("non-primitive method in F2 (F3 work)");
                }
                int primIdx = static_cast<int>(marker & ((1LL << 62) - 1));
                auto fn = rt_.registry().at(primIdx);
                // F6 v3 A note: this primitive call MAY itself end up
                // invoking invokeBlock() (e.g. ifTrue:, thenDo:, value)
                // which spins up a FRESH ExecutionEngine on the C++ stack.
                // That secondary engine has its own independent frames_
                // and does not interact with ours. The bounded primitive-
                // nesting depth makes that acceptable for F6 v3 A; the
                // unbounded user-method recursion was the actual target of
                // this refactor and is now gone.
                auto* result = fn(rt_, ctx, recv, sendArgs, argcOp);
                f.opStack.push_back(result ? result : PROTO_NONE);
                break;
            }
            case Op::JUMP:          f.pc += static_cast<std::size_t>(arg) * kInstrSize; break;
            case Op::JUMP_IF_TRUE: {
                auto* v = f.opStack.back(); f.opStack.pop_back();
                if (v == PROTO_TRUE) f.pc += static_cast<std::size_t>(arg) * kInstrSize;
                break;
            }
            case Op::JUMP_IF_FALSE: {
                auto* v = f.opStack.back(); f.opStack.pop_back();
                if (v == PROTO_FALSE) f.pc += static_cast<std::size_t>(arg) * kInstrSize;
                break;
            }
            case Op::PUSH_CAPTURED: {
                // arg = constant pool index of the (interned) symbol name.
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                const proto::ProtoObject* val = f.capturedDict
                    ? f.capturedDict->getAttribute(ctx, sym)
                    : nullptr;
                f.opStack.push_back(val ? val : PROTO_NONE);
                break;
            }
            case Op::STORE_CAPTURED: {
                if (f.opStack.empty())
                    throw std::runtime_error("STORE_CAPTURED with empty stack");
                const proto::ProtoObject* val = f.opStack.back();
                f.opStack.pop_back();
                if (!f.capturedDict)
                    throw std::runtime_error("STORE_CAPTURED without captured dict");
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                const_cast<proto::ProtoObject*>(f.capturedDict)->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_GLOBAL: {
                // arg = constant pool index of the (interned) global name.
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                auto* g = rt_.globals();
                auto* val = g ? g->getAttribute(ctx, sym) : nullptr;
                if (!val || val == PROTO_NONE)
                    throw std::runtime_error("undefined global: " + nameStr);
                f.opStack.push_back(val);
                break;
            }
            case Op::STORE_GLOBAL: {
                if (f.opStack.empty())
                    throw std::runtime_error("STORE_GLOBAL with empty stack");
                const proto::ProtoObject* val = f.opStack.back();
                f.opStack.pop_back();
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                auto* g = rt_.globals();
                if (!g) throw std::runtime_error("globals not initialized");
                g->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_INSTVAR: {
                // F4-U5: read an instance variable from `self`. `self` is
                // populated by runWithArgs into slot 0 of locals (see the
                // user-method dispatch path in SEND_*). arg is the constant-
                // pool index of the (interned) inst-var name symbol.
                //
                // We store inst vars under a mangled key ("_iv_<name>") so
                // their storage does NOT collide with same-named method
                // selectors in the receiver's prototype chain. Without this,
                // `Counter >> value ^ value.` would have the message-send
                // `c value` find the integer attribute stored by STORE_INSTVAR
                // instead of the method installed on the class, since
                // getAttribute walks own-attrs first.
                const std::string& nameStr = f.m->constSymbol(arg);
                std::string mangled = "_iv_";
                mangled += nameStr;
                auto* sym = ctx->fromUTF8String(mangled.c_str())->asString(ctx);
                if (f.locals.empty())
                    throw std::runtime_error("PUSH_INSTVAR with no self");
                const proto::ProtoObject* self = f.locals[0];
                auto* val = self ? self->getAttribute(ctx, sym) : nullptr;
                f.opStack.push_back(val ? val : PROTO_NONE);
                break;
            }
            case Op::STORE_INSTVAR: {
                // F4-U5: write an instance variable on `self`. Mirror of
                // PUSH_INSTVAR; pops the value-to-store from the operand
                // stack and setAttribute's it under the mangled "_iv_<name>"
                // key on slot-0 self. See PUSH_INSTVAR for rationale.
                if (f.opStack.empty())
                    throw std::runtime_error("STORE_INSTVAR empty stack");
                const proto::ProtoObject* val = f.opStack.back();
                f.opStack.pop_back();
                const std::string& nameStr = f.m->constSymbol(arg);
                std::string mangled = "_iv_";
                mangled += nameStr;
                auto* sym = ctx->fromUTF8String(mangled.c_str())->asString(ctx);
                if (f.locals.empty())
                    throw std::runtime_error("STORE_INSTVAR with no self");
                const proto::ProtoObject* self = f.locals[0];
                if (!self)
                    throw std::runtime_error("STORE_INSTVAR self is null");
                const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, sym, val);
                break;
            }
            default:
                throw std::runtime_error(
                    "ExecutionEngine: unimplemented opcode at pc=" +
                    std::to_string(f.pc - kInstrSize));
        }
    }
    // frames_ drained without hitting a RETURN at the top frame — this
    // can happen if a top-level module's bytes simply run out (the
    // implicit RETURN_TOP trailer is normally emitted by the compiler,
    // but be defensive).
    return PROTO_NONE;
    } catch (DebuggerHalt& h) {
        // F6 v3 A: enter the debugger session on the CURRENT top frame.
        // The legacy engine captured pc/stack/locals from local C++ vars;
        // we now read them from frames_.back() which holds equivalent
        // state. If frames_ is empty (shouldn't happen — the halt was
        // thrown from inside a primitive called by some active frame),
        // fall back to a synthetic empty frame to keep the debugger
        // contract.
        DebugFrame dframe;
        if (!frames_.empty()) {
            Frame& f = frames_.back();
            dframe.module = f.m;
            dframe.pc = f.pc;
            dframe.stack.assign(f.opStack.begin(), f.opStack.end());
            dframe.locals.assign(f.locals.begin(), f.locals.end());
        }
        rt_.debugger().enterSession(rt_, std::move(dframe), h.reason());
        // After the session resumes (user typed c/s/n/f), fall back through
        // the outer while(true) and continue executing from the current pc.
        // The halt primitive pushes PROTO_NONE as its return value into the
        // caller's stack via the SEND_UNARY handler, but throwing aborted
        // that. Push PROTO_NONE now so the operand stack matches what the
        // compiler expects after a unary send.
        if (!frames_.empty()) {
            frames_.back().opStack.push_back(PROTO_NONE);
        }
        continue;
    }
    } // outer while(true)
}

// ---------------------------------------------------------------------------
// F6 v3 B: snapshot/restore round-trip for cooperative yield.
//
// Encoding (one ProtoObject per frame, packaged into a ProtoList):
//   pc            -> ProtoInteger (long long via ctx->fromLong)
//   op_stack      -> ProtoList of operand-stack contents (oldest at index 0)
//   locals        -> ProtoList of local-slot contents; nullptr slots become
//                    PROTO_NONE so the list size equals locals.size()
//   self_obj      -> the frame's self, or PROTO_NONE
//   captured_dict -> the frame's captured-vars environment, or PROTO_NONE
//   m_ptr         -> ExternalPointer wrapping the const BytecodeModule*,
//                    finalizer = nullptr (modules are owned elsewhere)
//
// Keys are interned symbols cached in function-local statics so the symbol
// table is hit at most once per process per key.
// ---------------------------------------------------------------------------

const proto::ProtoObject*
ExecutionEngine::snapshotFrames(proto::ProtoContext* ctx) const {
    static const proto::ProtoString* pcKey       = proto::ProtoString::createSymbol(ctx, "pc");
    static const proto::ProtoString* opStackKey  = proto::ProtoString::createSymbol(ctx, "op_stack");
    static const proto::ProtoString* localsKey   = proto::ProtoString::createSymbol(ctx, "locals");
    static const proto::ProtoString* selfKey     = proto::ProtoString::createSymbol(ctx, "self_obj");
    static const proto::ProtoString* capturedKey = proto::ProtoString::createSymbol(ctx, "captured_dict");
    static const proto::ProtoString* mPtrKey     = proto::ProtoString::createSymbol(ctx, "m_ptr");

    const proto::ProtoList* result = ctx->newList();
    for (const Frame& fr : frames_) {
        // Each frame becomes a fresh mutable child of objectProto. This
        // mirrors how Object>>asActor builds the actor wrapper and how the
        // SEND fast-path builds message envelopes — see the
        // `bootstrap.objectProto->newChild(ctx, /*isMutable=*/true)` pattern
        // elsewhere in this file.
        auto* frameObj = const_cast<proto::ProtoObject*>(rt_.bootstrap().objectProto)
            ->newChild(ctx, /*isMutable=*/true);

        // pc
        frameObj->setAttribute(
            ctx, pcKey,
            ctx->fromLong(static_cast<long long>(fr.pc)));

        // op_stack as ProtoList (preserve order: index 0 = bottom of stack).
        const proto::ProtoList* opList = ctx->newList();
        for (const proto::ProtoObject* v : fr.opStack) {
            opList = opList->appendLast(ctx, v ? v : PROTO_NONE);
        }
        frameObj->setAttribute(ctx, opStackKey, opList->asObject(ctx));

        // locals as ProtoList (size == locals.size(), nullptr -> PROTO_NONE).
        const proto::ProtoList* locList = ctx->newList();
        for (const proto::ProtoObject* v : fr.locals) {
            locList = locList->appendLast(ctx, v ? v : PROTO_NONE);
        }
        frameObj->setAttribute(ctx, localsKey, locList->asObject(ctx));

        // self_obj
        frameObj->setAttribute(
            ctx, selfKey,
            fr.selfObj ? fr.selfObj : PROTO_NONE);

        // captured_dict
        frameObj->setAttribute(
            ctx, capturedKey,
            fr.capturedDict ? fr.capturedDict : PROTO_NONE);

        // m_ptr — borrow the BytecodeModule pointer; no finalizer because the
        // module is owned by STRuntime::loadedModules or by the original
        // top-level caller. const_cast is safe here: the snapshot only reads
        // the module through the const protoST::BytecodeModule* recovered on
        // restoreFrames.
        frameObj->setAttribute(
            ctx, mPtrKey,
            ctx->fromExternalPointer(
                const_cast<protoST::BytecodeModule*>(fr.m),
                /*finalizer=*/nullptr));

        result = result->appendLast(ctx, frameObj);
    }
    return result->asObject(ctx);
}

void
ExecutionEngine::restoreFrames(proto::ProtoContext* ctx,
                               const proto::ProtoObject* snapshot) {
    static const proto::ProtoString* pcKey       = proto::ProtoString::createSymbol(ctx, "pc");
    static const proto::ProtoString* opStackKey  = proto::ProtoString::createSymbol(ctx, "op_stack");
    static const proto::ProtoString* localsKey   = proto::ProtoString::createSymbol(ctx, "locals");
    static const proto::ProtoString* selfKey     = proto::ProtoString::createSymbol(ctx, "self_obj");
    static const proto::ProtoString* capturedKey = proto::ProtoString::createSymbol(ctx, "captured_dict");
    static const proto::ProtoString* mPtrKey     = proto::ProtoString::createSymbol(ctx, "m_ptr");

    if (!snapshot)
        throw std::runtime_error("restoreFrames: snapshot is null");
    auto* asList = snapshot->asList(ctx);
    if (!asList)
        throw std::runtime_error("restoreFrames: snapshot is not a list");

    frames_.clear();
    const unsigned long n = asList->getSize(ctx);
    frames_.reserve(std::max<std::size_t>(64, static_cast<std::size_t>(n)));

    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* frameObj =
            asList->getAt(ctx, static_cast<int>(i));
        if (!frameObj)
            throw std::runtime_error("restoreFrames: null frame entry");

        Frame fr;

        // pc
        auto* pcVal = frameObj->getAttribute(ctx, pcKey);
        if (!pcVal || pcVal == PROTO_NONE)
            throw std::runtime_error("restoreFrames: missing pc");
        fr.pc = static_cast<std::size_t>(pcVal->asLong(ctx));

        // op_stack
        auto* opStackVal = frameObj->getAttribute(ctx, opStackKey);
        auto* opList = opStackVal ? opStackVal->asList(ctx) : nullptr;
        if (!opList)
            throw std::runtime_error("restoreFrames: missing op_stack");
        const unsigned long opN = opList->getSize(ctx);
        fr.opStack.reserve(std::max<std::size_t>(64, static_cast<std::size_t>(opN)));
        for (unsigned long j = 0; j < opN; ++j)
            fr.opStack.push_back(opList->getAt(ctx, static_cast<int>(j)));

        // locals
        auto* locVal = frameObj->getAttribute(ctx, localsKey);
        auto* locList = locVal ? locVal->asList(ctx) : nullptr;
        if (!locList)
            throw std::runtime_error("restoreFrames: missing locals");
        const unsigned long locN = locList->getSize(ctx);
        fr.locals.reserve(std::max<std::size_t>(16, static_cast<std::size_t>(locN)));
        for (unsigned long j = 0; j < locN; ++j)
            fr.locals.push_back(locList->getAt(ctx, static_cast<int>(j)));

        // self_obj — convert PROTO_NONE sentinel back to nullptr to mirror
        // the convention used by runWithArgs (selfObj may legitimately be
        // nullptr at the top frame for module-level code).
        auto* selfVal = frameObj->getAttribute(ctx, selfKey);
        fr.selfObj = (selfVal == PROTO_NONE) ? nullptr : selfVal;

        // captured_dict — same nullptr-vs-PROTO_NONE convention.
        auto* capVal = frameObj->getAttribute(ctx, capturedKey);
        fr.capturedDict = (capVal == PROTO_NONE) ? nullptr : capVal;

        // m_ptr
        auto* mPtrVal = frameObj->getAttribute(ctx, mPtrKey);
        if (!mPtrVal || mPtrVal == PROTO_NONE)
            throw std::runtime_error("restoreFrames: missing m_ptr");
        auto* ep = mPtrVal->asExternalPointer(ctx);
        if (!ep)
            throw std::runtime_error("restoreFrames: m_ptr is not an ExternalPointer");
        fr.m = static_cast<const BytecodeModule*>(ep->getPointer(ctx));
        if (!fr.m)
            throw std::runtime_error("restoreFrames: m_ptr decoded to nullptr");

        frames_.push_back(std::move(fr));
    }
}

} // namespace protoST
