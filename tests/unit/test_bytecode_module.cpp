#include <catch2/catch_all.hpp>
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

using protoST::BytecodeModule;
using protoST::Op;

TEST_CASE("BytecodeModule round-trips emit and decode", "[bytecode]") {
    BytecodeModule m;
    auto idx = m.addInteger(42);
    REQUIRE(idx == 0);
    m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
    m.emit(Op::RETURN_TOP, 0);

    REQUIRE(m.bytes().size() == 4);
    REQUIRE(static_cast<Op>(m.bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(m.bytes()[1] == 0);
    REQUIRE(static_cast<Op>(m.bytes()[2]) == Op::RETURN_TOP);

    REQUIRE(m.constInteger(0) == 42);
}

TEST_CASE("BytecodeModule supports symbol interning by string", "[bytecode]") {
    BytecodeModule m;
    auto a = m.internSymbol("value");
    auto b = m.internSymbol("value");
    auto c = m.internSymbol("at:put:");
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(m.constSymbol(a) == "value");
    REQUIRE(m.constSymbol(c) == "at:put:");
}
