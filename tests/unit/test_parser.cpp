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

TEST_CASE("Parser: unary chain", "[parser]") {
    Parser P("foo printNl size.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& outer = m->children[0];                 // UnarySend size
    REQUIRE(outer->kind == NodeKind::UnarySend);
    REQUIRE(outer->text == "size");
    auto& inner = outer->children[0];             // UnarySend printNl
    REQUIRE(inner->kind == NodeKind::UnarySend);
    REQUIRE(inner->text == "printNl");
    REQUIRE(inner->children[0]->kind == NodeKind::Identifier);
    REQUIRE(inner->children[0]->text == "foo");
}

TEST_CASE("Parser: binary message left-assoc", "[parser]") {
    Parser P("1 + 2 + 3.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& top = m->children[0];                   // (1+2)+3
    REQUIRE(top->kind == NodeKind::BinarySend);
    REQUIRE(top->text == "+");
    REQUIRE(top->children[1]->kind == NodeKind::IntegerLit);
    REQUIRE(top->children[1]->intValue == 3);
    auto& left = top->children[0];
    REQUIRE(left->kind == NodeKind::BinarySend);
    REQUIRE(left->children[0]->intValue == 1);
    REQUIRE(left->children[1]->intValue == 2);
}

TEST_CASE("Parser: keyword send aggregates parts", "[parser]") {
    Parser P("dict at: 1 put: 'one'.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& kw = m->children[0];
    REQUIRE(kw->kind == NodeKind::KeywordSend);
    REQUIRE(kw->text == "at:put:");
    REQUIRE(kw->children.size() == 3);            // receiver + 2 args
    REQUIRE(kw->children[0]->kind == NodeKind::Identifier);
    REQUIRE(kw->children[1]->intValue == 1);
    REQUIRE(kw->children[2]->kind == NodeKind::StringLit);
}

TEST_CASE("Parser: precedence unary > binary > keyword", "[parser]") {
    Parser P("x at: y size + 1 put: z.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& kw = m->children[0];
    REQUIRE(kw->kind == NodeKind::KeywordSend);
    REQUIRE(kw->text == "at:put:");
    auto& arg1 = kw->children[1];                 // y size + 1
    REQUIRE(arg1->kind == NodeKind::BinarySend);
    REQUIRE(arg1->text == "+");
    REQUIRE(arg1->children[0]->kind == NodeKind::UnarySend); // y size
}

TEST_CASE("Parser: cascade collects messages on same receiver", "[parser]") {
    Parser P("Transcript show: 'a'; show: 'b'; cr.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& casc = m->children[0];
    REQUIRE(casc->kind == NodeKind::Cascade);
    REQUIRE(casc->children.size() == 4); // receiver + 3 partial sends
    REQUIRE(casc->children[0]->kind == NodeKind::Identifier);
    REQUIRE(casc->children[1]->kind == NodeKind::KeywordSend);
    REQUIRE(casc->children[2]->kind == NodeKind::KeywordSend);
    REQUIRE(casc->children[3]->kind == NodeKind::UnarySend);
    // partial sends have NO receiver in children[0]
    REQUIRE(casc->children[1]->children.size() == 1); // one keyword arg, no receiver
    REQUIRE(casc->children[3]->children.empty());
}

TEST_CASE("Parser: block with arguments and locals", "[parser]") {
    Parser P("[ :a :b | | x | a + b ].");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& blk = m->children[0];
    REQUIRE(blk->kind == NodeKind::Block);
    REQUIRE(blk->intValue == 2);                 // arg count
    REQUIRE(blk->stringList == std::vector<std::string>{"a","b","x"});
    REQUIRE(blk->children.size() == 1);
    REQUIRE(blk->children[0]->kind == NodeKind::BinarySend);
}

TEST_CASE("Parser: empty block", "[parser]") {
    Parser P("[].");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& blk = m->children[0];
    REQUIRE(blk->kind == NodeKind::Block);
    REQUIRE(blk->intValue == 0);
    REQUIRE(blk->children.empty());
}
