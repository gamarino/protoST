#pragma once
#include <cstddef>
#include <cstdint>

namespace protoST {

enum class Op : uint8_t {
    NOP             = 0,
    // Stack and constants
    PUSH_CONST      = 1,    // arg = constant pool index
    PUSH_LOCAL      = 2,    // arg = slot index
    STORE_LOCAL     = 3,    // arg = slot index
    DUP             = 4,
    POP             = 5,
    // Values
    PUSH_SELF       = 6,
    PUSH_SUPER      = 7,
    PUSH_NIL        = 8,
    PUSH_TRUE       = 9,
    PUSH_FALSE      = 10,
    // Sends
    SEND_UNARY      = 11,   // arg = selector const index
    SEND_BINARY     = 12,   // arg = selector const index; 1 arg on stack
    SEND_KEYWORD    = 13,   // arg = selector const index; argc encoded in selector
    SEND_SUPER      = 14,
    // Control flow
    JUMP            = 15,   // arg = forward offset in instructions (i.e. multiple of 2 bytes)
    JUMP_IF_TRUE    = 16,
    JUMP_IF_FALSE   = 17,
    // Return
    RETURN          = 18,
    RETURN_TOP      = 19,   // pops top, returns
    // Blocks
    PUSH_BLOCK      = 20,   // arg = bytecode-module-table index
    // Cascade helper
    DUP_RECEIVER    = 21,   // dup the value at depth = arg
    // Closures (F3)
    PUSH_CAPTURED   = 22,   // arg = constant pool index (symbol name)
    STORE_CAPTURED  = 23,   // arg = constant pool index (symbol name)
    // User classes (F4)
    PUSH_GLOBAL     = 24,   // arg = constant pool symbol index (name of global)
    STORE_GLOBAL    = 25,   // arg = constant pool symbol index
    PUSH_INSTVAR    = 26,   // arg = constant pool symbol index (name of inst var, accessed on self/slot 0)
    STORE_INSTVAR   = 27,   // arg = constant pool symbol index
    // Extend for >256-index args
    EXTEND          = 254,
    // Debugger primitive guard
    HALT            = 255,
};

inline constexpr size_t kInstrSize = 2;

} // namespace protoST
