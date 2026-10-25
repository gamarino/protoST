#pragma once
#include <string>

namespace protoST {

class BytecodeModule;

// A readable listing of a compiled module and of every module it holds (the
// bodies of its blocks and methods, nested ones included), for
// `protost --disasm script.st` and docs/VM.md.
//
// The output depends only on the bytecode: no addresses, no interned-object
// pointers and nothing else that changes from one run to the next, so it can
// be compared with golden files (docs/vm-examples/disasm_examples.py).
//
// Format, one block per module, the top-level module first:
//
//   module <path>: <debug name>
//     args N, locals M (slot names), stack 48 + extra
//     [defining class, call-form signature, scoped locals, write groups]
//     constants:
//       #i  <kind>  <value>
//     code:
//       <word>  <OPCODE>  <operand>  ; <decoded operand>
//
// <path> names a module by its position: `main` is the top level,
// `main.3` its block 3, `main.3.0` block 0 of that body. `PUSH_BLOCK 3`
// in `main` builds a closure (or a method wrapper) over `main.3`.
// <word> is the index of the instruction's first 2-byte word; jump operands
// count words, and a jump's comment gives its absolute target word. An
// instruction with an EXTEND prefix is listed once, at its first word, with
// the combined operand and a `(wide)` note.
std::string disassemble(const BytecodeModule& m, const std::string& path = "main");

} // namespace protoST
