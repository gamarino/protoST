#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"
#include "frontend/ASTPrinter.h"

TEST_CASE("ASTPrinter: integer literal", "[ast][printer]") {
    protoST::Parser P("42.");
    auto m = P.parseModule();
    auto out = protoST::astToString(*m);
    REQUIRE(out == "(module\n  (int 42))\n");
}

TEST_CASE("ASTPrinter: binary send", "[ast][printer]") {
    protoST::Parser P("1 + 2.");
    auto m = P.parseModule();
    auto out = protoST::astToString(*m);
    REQUIRE(out == "(module\n  (binary + (int 1) (int 2)))\n");
}

TEST_CASE("ASTPrinter: method decl", "[ast][printer]") {
    protoST::Parser P("Counter >> increment value := value + 1.");
    auto m = P.parseModule();
    auto out = protoST::astToString(*m);
    REQUIRE(out.find("(method-decl Counter increment 0 ()") != std::string::npos);
    REQUIRE(out.find("(assign value (binary + (id value) (int 1)))") != std::string::npos);
}
