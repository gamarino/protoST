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
