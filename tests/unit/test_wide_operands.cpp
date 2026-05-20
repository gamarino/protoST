// BL-2: wide-operand support — lifting the 256-local/constant ISA ceiling.
//
// protoST bytecode words are [opcode][8-bit operand]. The 8-bit operand caps
// constant-pool indices, local-slot indices, symbol indices and jump targets
// at 256. BL-2 wires the EXTEND prefix (Python EXTENDED_ARG style): when an
// operand exceeds 255 the compiler emits one or more EXTEND words carrying the
// high byte(s); the engine latches those bits and combines them with the real
// word's low byte. Instructions become variable width (2 or 4+ bytes).
//
// These tests prove:
//   * a module with > 256 local variables compiles and runs,
//   * a module with > 256 distinct constants compiles and runs,
//   * EXTEND encode/decode round-trips through emitWide and the engine,
//   * the line map stays correct once an instruction sits past pc 255.
#include <catch2/catch_all.hpp>
#include <string>

#include "protoST/STRuntime.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

using protoST::Parser;
using protoST::Compiler;
using protoST::BytecodeModule;
using protoST::Op;

namespace {
std::unique_ptr<BytecodeModule> compileSrc(const std::string& src) {
    Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return bc;
}
}  // namespace

TEST_CASE("BL-2: emitWide round-trips an operand wider than 8 bits",
          "[bytecode][bl2][wide]") {
    BytecodeModule m;
    // A small operand emits a single 2-byte word (no EXTEND prefix).
    m.emitWide(Op::PUSH_LOCAL, 7);
    REQUIRE(m.bytes().size() == 2);
    REQUIRE(static_cast<Op>(m.bytes()[0]) == Op::PUSH_LOCAL);
    REQUIRE(m.bytes()[1] == 7);

    // An operand of 300 needs one EXTEND prefix: 300 = 0x012C → hi=0x01, lo=0x2C.
    BytecodeModule m2;
    m2.emitWide(Op::PUSH_CONST, 300);
    REQUIRE(m2.bytes().size() == 4);
    REQUIRE(static_cast<Op>(m2.bytes()[0]) == Op::EXTEND);
    REQUIRE(m2.bytes()[1] == 0x01);
    REQUIRE(static_cast<Op>(m2.bytes()[2]) == Op::PUSH_CONST);
    REQUIRE(m2.bytes()[3] == 0x2C);
}

TEST_CASE("BL-2: a module with more than 256 local variables runs correctly",
          "[engine][bl2][wide][locals]") {
    // Declare 300 module-level temps t0..t299, assign each its own index,
    // then sum three of them whose slot indices straddle the 256 boundary:
    // t10 (=10), t255 (=255), t299 (=299) → 564.
    protoST::STRuntime rt;
    std::string src;
    for (int i = 0; i < 300; ++i)
        src += "t" + std::to_string(i) + " := " + std::to_string(i) + ". ";
    src += "(t10 + t255) + t299.";

    auto bc = compileSrc(src);
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 10 + 255 + 299);
}

TEST_CASE("BL-2: a module with more than 256 distinct constants runs correctly",
          "[engine][bl2][wide][constants]") {
    // 300 distinct integer literals 1000..1299 each stored into its own temp.
    // The literal 1290 lands at a constant-pool index well past 255, so its
    // PUSH_CONST needs an EXTEND prefix; the engine must still materialize it.
    protoST::STRuntime rt;
    std::string src;
    for (int i = 0; i < 300; ++i)
        src += "c" + std::to_string(i) + " := " + std::to_string(1000 + i)
             + ". ";
    src += "c290.";

    auto bc = compileSrc(src);
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 1290);
}

TEST_CASE("BL-2: a method body with more than 256 locals runs correctly",
          "[engine][bl2][wide][locals]") {
    // The hard case: a method sub-module. Slot 0 is self, slot 1 is the arg,
    // then 300 method temps — the last temps need wide STORE_LOCAL/PUSH_LOCAL.
    protoST::STRuntime rt;
    std::string body;
    for (int i = 0; i < 300; ++i)
        body += "u" + std::to_string(i) + " := " + std::to_string(i) + ". ";
    std::string src =
        "Object subclass: #Big. "
        "Big >> run: n " + body + " ^ (u5 + u270) + n. "
        "Big newChild run: 1000.";

    auto bc = compileSrc(src);
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 5 + 270 + 1000);
}

TEST_CASE("BL-2: a wide JUMP operand decodes and branches correctly",
          "[engine][bl2][wide][jump]") {
    // The compiler does not currently emit JUMP (ifTrue:/whileTrue: are block
    // sends), so exercise the wide JUMP path by hand: a JUMP whose operand is
    // 130 (an instruction count > 255 bytes once doubled) must skip exactly
    // 130 instruction words. Here we keep it modest but still wide enough to
    // need an EXTEND prefix (>255 forces the prefix; 256 is the threshold).
    //
    // Layout: PUSH_CONST 0 (=7); JUMP <skip>; <skip> filler NOPs; PUSH_CONST 1
    // (=99); RETURN_TOP. With the jump taken, the 7 is discarded path is never
    // hit — the JUMP lands on the PUSH_CONST 99, returning 99.
    protoST::STRuntime rt;
    BytecodeModule m;
    m.addInteger(7);    // const 0
    m.addInteger(99);   // const 1

    // PUSH_CONST 0  — leaves 7 on the stack.
    m.emit(Op::PUSH_CONST, 0);
    // JUMP over the next N filler NOP words. We want a wide operand, so make
    // the skip count exceed 255: emit 300 NOPs and jump past all of them.
    const unsigned int skip = 300;
    m.emitWide(Op::JUMP, skip);
    for (unsigned int i = 0; i < skip; ++i)
        m.emit(Op::POP, 0);          // would underflow the stack if reached
    // Landing site: discard the 7, push 99, return it.
    m.emit(Op::POP, 0);
    m.emit(Op::PUSH_CONST, 1);
    m.emit(Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("BL-2: line map stays correct when an instruction sits past pc 255",
          "[compiler][bl2][wide][lines]") {
    // Emit enough statements that total bytecode exceeds 255 bytes, with the
    // final statement on a distinct, known source line. lineForPc /
    // firstPcForLine must round-trip for that high-pc instruction.
    std::string src;
    int line = 0;
    for (int i = 0; i < 120; ++i) {
        src += std::to_string(i) + " + " + std::to_string(i) + ".\n";
        ++line;
    }
    src += "42 + 0.\n";        // the marker statement, on line `line+1`
    int markerLine = line + 1;

    auto bc = compileSrc(src);
    REQUIRE(bc->bytes().size() > 255);  // an instruction definitely past 255

    size_t pc = bc->firstPcForLine(markerLine);
    REQUIRE(pc != SIZE_MAX);
    REQUIRE(pc > 255);                          // the marker is past pc 255
    REQUIRE(bc->lineForPc(pc) == markerLine);   // round-trips

    // instrLines_ and instrStartPc_ stay parallel and the byte offsets are
    // monotonically increasing.
    REQUIRE(bc->instrLines().size() == bc->instrStartPc().size());
    for (size_t i = 1; i < bc->instrStartPc().size(); ++i)
        REQUIRE(bc->instrStartPc()[i] > bc->instrStartPc()[i - 1]);

    // lineForPc out of range still returns 0 (unknown).
    REQUIRE(bc->lineForPc(bc->bytes().size() + 100) == 0);
}
