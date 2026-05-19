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

TEST_CASE("Parser: literal integers", "[parser]") {
    Parser P("42.");
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    REQUIRE(mod->children.size() == 1);
    auto& expr = mod->children[0];
    REQUIRE(expr->kind == NodeKind::IntegerLit);
    REQUIRE(expr->intValue == 42);
}

TEST_CASE("Parser: identifiers and self/super/nil/true/false", "[parser]") {
    auto parseOne = [](const char* src) {
        Parser P(src);
        auto m = P.parseModule();
        REQUIRE(P.errors().empty());
        REQUIRE(m->children.size() == 1);
        return std::move(m->children[0]);
    };
    REQUIRE(parseOne("foo.")->kind == NodeKind::Identifier);
    REQUIRE(parseOne("self.")->kind == NodeKind::Self);
    REQUIRE(parseOne("super.")->kind == NodeKind::Super);
    REQUIRE(parseOne("nil.")->kind == NodeKind::NilLit);
    REQUIRE(parseOne("true.")->kind == NodeKind::TrueLit);
    REQUIRE(parseOne("false.")->kind == NodeKind::FalseLit);
}

TEST_CASE("Parser: parenthesised expression preserves inner kind", "[parser]") {
    Parser P("(42).");
    auto m = P.parseModule();
    REQUIRE(P.errors().empty());
    REQUIRE(m->children[0]->kind == NodeKind::IntegerLit);
}
