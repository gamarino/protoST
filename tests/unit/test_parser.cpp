#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"

using protoST::Parser;
using protoST::ast::NodeKind;

TEST_CASE("Parser scaffold builds empty module from empty source", "[parser]") {
    Parser P("");
    auto mod = P.parseModule();
    REQUIRE(mod != nullptr);
    REQUIRE(mod->kind == NodeKind::Module);
    REQUIRE(mod->children.empty());
    REQUIRE(P.errors().empty());
}

TEST_CASE("Parser reports unexpected token", "[parser]") {
    Parser P("@@@");          // '@' is binop but not legal at top level
    auto mod = P.parseModule();
    REQUIRE(!P.errors().empty());
}
