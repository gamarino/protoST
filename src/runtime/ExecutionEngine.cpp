#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Opcodes.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

const proto::ProtoObject*
ExecutionEngine::run(proto::ProtoContext* /*ctx*/,
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
            default:
                throw std::runtime_error(
                    "ExecutionEngine: unimplemented opcode at pc=" +
                    std::to_string(pc - kInstrSize));
        }
    }
    return PROTO_NONE;
}

} // namespace protoST
