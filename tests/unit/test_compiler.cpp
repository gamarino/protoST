#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

using protoST::Parser;
using protoST::Compiler;
using protoST::BytecodeModule;
using protoST::Op;

TEST_CASE("Compiler: empty module emits a single RETURN", "[compiler]") {
    Parser P("");
    auto ast = P.parseModule();
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->bytes().size() == 4);
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::RETURN_TOP);
}

TEST_CASE("Compiler: integer literal pushes from const pool", "[compiler]") {
    Parser P("42.");
    auto ast = P.parseModule();
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(bc->constInteger(0) == 42);
}

TEST_CASE("Compiler: nil/true/false push the dedicated opcodes", "[compiler]") {
    auto compile = [](const char* src) {
        Parser P(src);
        Compiler C; auto bc = C.compileModule(*P.parseModule());
        REQUIRE(!C.hasErrors());
        return bc;
    };
    REQUIRE(static_cast<Op>(compile("nil.")->bytes()[0])   == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(compile("true.")->bytes()[0])  == Op::PUSH_TRUE);
    REQUIRE(static_cast<Op>(compile("false.")->bytes()[0]) == Op::PUSH_FALSE);
}

TEST_CASE("Compiler: assignment creates slot and emits STORE_LOCAL", "[compiler]") {
    Parser P("x := 42. x.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // Sequence (STORE_LOCAL pops; DUP keeps assignment-as-expression value on stack):
    //   PUSH_CONST 0   (42)
    //   DUP
    //   STORE_LOCAL 0  (x)
    //   POP            (top-level statement separator)
    //   PUSH_LOCAL 0
    //   RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::DUP);
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::STORE_LOCAL);
    REQUIRE(bc->bytes()[5] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[6]) == Op::POP);
    REQUIRE(static_cast<Op>(bc->bytes()[8]) == Op::PUSH_LOCAL);
    REQUIRE(bc->bytes()[9] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[10]) == Op::RETURN_TOP);
}

TEST_CASE("Compiler: unary send emits SEND_UNARY", "[compiler]") {
    Parser P("nil printNl.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::SEND_UNARY);
    REQUIRE(bc->constSymbol(bc->bytes()[3]) == "printNl");
}

TEST_CASE("Compiler: binary send emits SEND_BINARY", "[compiler]") {
    Parser P("1 + 2.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_CONST 0 (1), PUSH_CONST 1 (2), SEND_BINARY (sym +)
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::SEND_BINARY);
    REQUIRE(bc->constSymbol(bc->bytes()[5]) == "+");
}

TEST_CASE("Compiler: keyword send emits SEND_KEYWORD", "[compiler]") {
    Parser P("nil at: 1 put: 2.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_NIL, PUSH_CONST(1), PUSH_CONST(2), SEND_KEYWORD sym(at:put:)
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[6]) == Op::SEND_KEYWORD);
    REQUIRE(bc->constSymbol(bc->bytes()[7]) == "at:put:");
}

TEST_CASE("Compiler: cascade emits dup/pop dance and keeps last value", "[compiler]") {
    Parser P("nil yourself; printNl; size.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // At minimum: opcode sequence ends with the last SEND_UNARY (size) and not extra DUPs
    auto last_send = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::SEND_UNARY &&
            bc->constSymbol(bc->bytes()[i+1]) == "size") last_send = true;
    }
    REQUIRE(last_send);
}

TEST_CASE("Compiler: block compiles to a sub-module referenced by PUSH_BLOCK", "[compiler]") {
    Parser P("[ :a :b | a + b ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_BLOCK 0 ; RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_BLOCK);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    // body should end with RETURN_TOP after PUSH_LOCAL 0, PUSH_LOCAL 1, SEND_BINARY +
    REQUIRE(static_cast<Op>(blk.bytes()[0]) == Op::PUSH_LOCAL);
    REQUIRE(static_cast<Op>(blk.bytes()[2]) == Op::PUSH_LOCAL);
    REQUIRE(static_cast<Op>(blk.bytes()[4]) == Op::SEND_BINARY);
}

TEST_CASE("Compiler: F2 hero expression parses and compiles", "[compiler]") {
    Parser P("(1 to: 100) inject: 0 into: [:a :b | a + b].");
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->bytes().size() >= 8);                      // non-trivial program
    REQUIRE(bc->numBlocks() == 1);                          // the [:a :b| ...] block
    // ends with RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[bc->bytes().size()-2]) == Op::RETURN_TOP);
}

TEST_CASE("Compiler analysis: simple module-level capture", "[compiler][closures]") {
    Parser P("sum := 0. i := 1. [ i <= 100 ].");
    Compiler C; C.analyseClosures(*P.parseModule());
    const auto& a = C.analysis();
    // 'i' is declared at module level AND used in the inner block -> captured.
    // 'sum' is declared at module level but NOT used in any inner block -> NOT captured.
    REQUIRE(a.moduleCaptured.count("i") == 1);
    REQUIRE(a.moduleCaptured.count("sum") == 0);
}

TEST_CASE("Compiler analysis: no inner blocks -> nothing captured", "[compiler][closures]") {
    Parser P("x := 1. y := 2.");
    Compiler C; C.analyseClosures(*P.parseModule());
    REQUIRE(C.analysis().moduleCaptured.empty());
}

TEST_CASE("Compiler analysis: block-local args are not captured (they are block-private)", "[compiler][closures]") {
    Parser P("[ :a :b | a + b ].");
    Compiler C; C.analyseClosures(*P.parseModule());
    // 'a' and 'b' are block-local args, not module-level.
    REQUIRE(C.analysis().moduleCaptured.empty());
}

TEST_CASE("Compiler analysis: nested blocks bubble free vars up", "[compiler][closures]") {
    // Inner block uses 'x' which is declared at module level — through a nested block.
    Parser P("x := 0. [ [ x ] ].");
    Compiler C; C.analyseClosures(*P.parseModule());
    REQUIRE(C.analysis().moduleCaptured.count("x") == 1);
}

TEST_CASE("Compiler emit: captured var uses PUSH_CAPTURED / STORE_CAPTURED", "[compiler][closures]") {
    // `i` is module-local AND used in inner block → captured
    // Inner block has no own locals; references `i` → should emit PUSH_CAPTURED with const idx of symbol "i"
    Parser P("i := 0. [ i ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    // Find at least one STORE_CAPTURED in the top-level module bytecode
    bool sawStoreCaptured = false;
    bool sawPushCapturedInOuter = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::STORE_CAPTURED) sawStoreCaptured = true;
        if (static_cast<Op>(bc->bytes()[i]) == Op::PUSH_CAPTURED) sawPushCapturedInOuter = true;
    }
    REQUIRE(sawStoreCaptured);

    // Inside the block sub-module, find PUSH_CAPTURED with the symbol "i"
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    bool sawPushCapturedInBlock = false;
    uint8_t pushedSymIdx = 255;
    for (size_t i = 0; i + 1 < blk.bytes().size(); i += 2) {
        if (static_cast<Op>(blk.bytes()[i]) == Op::PUSH_CAPTURED) {
            sawPushCapturedInBlock = true;
            pushedSymIdx = blk.bytes()[i+1];
        }
    }
    REQUIRE(sawPushCapturedInBlock);
    REQUIRE(blk.constSymbol(pushedSymIdx) == "i");
}

TEST_CASE("Compiler emit: non-captured locals still use PUSH_LOCAL", "[compiler][closures]") {
    // `x` is module-local, no inner block uses it → NOT captured → slot-vector path
    Parser P("x := 7. x.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // Should see PUSH_LOCAL and STORE_LOCAL, NOT PUSH_CAPTURED
    bool sawPushCapturedAnywhere = false;
    bool sawStoreCapturedAnywhere = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::PUSH_CAPTURED)  sawPushCapturedAnywhere = true;
        if (static_cast<Op>(bc->bytes()[i]) == Op::STORE_CAPTURED) sawStoreCapturedAnywhere = true;
    }
    REQUIRE_FALSE(sawPushCapturedAnywhere);
    REQUIRE_FALSE(sawStoreCapturedAnywhere);
}

TEST_CASE("Compiler emit: block-local args do NOT trigger captured path", "[compiler][closures]") {
    // `a` and `b` are block-private args; should be PUSH_LOCAL within block, no PUSH_CAPTURED.
    Parser P("[ :a :b | a + b ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    bool sawAnyCaptured = false;
    for (size_t i = 0; i + 1 < blk.bytes().size(); i += 2) {
        auto op = static_cast<Op>(blk.bytes()[i]);
        if (op == Op::PUSH_CAPTURED || op == Op::STORE_CAPTURED) sawAnyCaptured = true;
    }
    REQUIRE_FALSE(sawAnyCaptured);
}
