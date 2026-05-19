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
