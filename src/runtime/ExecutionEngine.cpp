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

    while (pc + 1 < bytes.size()) {
        const Op op = static_cast<Op>(bytes[pc]);
        // arg is decoded here for future opcodes; the F2 scaffold ignores it
        // for the implemented opcodes, but we keep pc advancing by kInstrSize.
        (void) bytes[pc + 1];
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
