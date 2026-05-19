#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Opcodes.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

const proto::ProtoObject*
ExecutionEngine::run(proto::ProtoContext* ctx,
                     const BytecodeModule& m,
                     const proto::ProtoObject* /*self*/) {
    const auto& bytes = m.bytes();
    std::size_t pc = 0;

    // F2: a plain operand stack. The "no std::vector" rule applies fully
    // when the actor model arrives in F6, which is out of scope for this task.
    std::vector<const proto::ProtoObject*> stack;
    stack.reserve(64);

    // F2: per-method locals as a small std::vector. Replaced with
    // ProtoSparseList in Task 50 when actors arrive (out of scope for F1+F2).
    std::vector<const proto::ProtoObject*> locals;
    locals.reserve(16);

    auto ensureLocal = [&](uint8_t slot) {
        if (slot >= locals.size())
            locals.resize(static_cast<size_t>(slot) + 1, PROTO_NONE);
    };

    while (pc + 1 < bytes.size()) {
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
            case Op::RETURN_TOP: {
                const proto::ProtoObject* r =
                    stack.empty() ? PROTO_NONE : stack.back();
                return r;
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
                // Use getPrototype (not getFirstParent) so tagged immediates
                // (SmallInteger, Boolean, inline String, ...) resolve to their
                // prototype via ProtoSpace::*Prototype slots set by bootstrap.
                auto* proto = recv->getPrototype(ctx);
                auto* attr  = (proto && proto != PROTO_NONE) ? proto->getAttribute(ctx, selSym) : nullptr;
                if (!attr || attr == PROTO_NONE) {
                    throw std::runtime_error("doesNotUnderstand: " + selStr);
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
            default:
                throw std::runtime_error(
                    "ExecutionEngine: unimplemented opcode at pc=" +
                    std::to_string(pc - kInstrSize));
        }
    }
    return PROTO_NONE;
}

} // namespace protoST
