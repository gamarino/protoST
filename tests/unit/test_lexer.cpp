#include <catch2/catch_all.hpp>
#include "frontend/Lexer.h"

using protoST::Lexer;
using protoST::TokenKind;

TEST_CASE("lexer skips whitespace and lexes identifiers", "[lexer]") {
    Lexer L("  counter  value42");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Identifier);
    REQUIRE(t1.text == "counter");
    REQUIRE(t1.line == 1);
    REQUIRE(t1.column == 3);

    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Identifier);
    REQUIRE(t2.text == "value42");

    REQUIRE(L.next().kind == TokenKind::EndOfFile);
}

TEST_CASE("lexer lexes positive integers", "[lexer]") {
    Lexer L("42 0 1000");
    auto t = L.next();
    REQUIRE(t.kind == TokenKind::Integer);
    REQUIRE(t.intValue == 42);
    REQUIRE(L.next().intValue == 0);
    REQUIRE(L.next().intValue == 1000);
}

TEST_CASE("lexer tracks line numbers across newlines", "[lexer]") {
    Lexer L("a\n b\n  c");
    REQUIRE(L.next().line == 1);
    auto t2 = L.next(); REQUIRE(t2.line == 2); REQUIRE(t2.column == 2);
    auto t3 = L.next(); REQUIRE(t3.line == 3); REQUIRE(t3.column == 3);
}

TEST_CASE("lexer recognizes single-char punctuation", "[lexer]") {
    Lexer L("( ) [ ] { } . ; ^ |");
    REQUIRE(L.next().kind == TokenKind::LParen);
    REQUIRE(L.next().kind == TokenKind::RParen);
    REQUIRE(L.next().kind == TokenKind::LBracket);
    REQUIRE(L.next().kind == TokenKind::RBracket);
    REQUIRE(L.next().kind == TokenKind::LBrace);
    REQUIRE(L.next().kind == TokenKind::RBrace);
    REQUIRE(L.next().kind == TokenKind::Period);
    REQUIRE(L.next().kind == TokenKind::Semicolon);
    REQUIRE(L.next().kind == TokenKind::Caret);
    REQUIRE(L.next().kind == TokenKind::Pipe);
}

TEST_CASE("lexer recognizes binary operators and := and >>", "[lexer]") {
    Lexer L("+ - * / = == ~= <= >= < > & , -> := >>");
    auto checkBin = [&](const char* expected) {
        auto t = L.next();
        REQUIRE(t.kind == TokenKind::BinaryOp);
        REQUIRE(t.text == expected);
    };
    checkBin("+"); checkBin("-"); checkBin("*"); checkBin("/");
    checkBin("="); checkBin("=="); checkBin("~="); checkBin("<=");
    checkBin(">="); checkBin("<"); checkBin(">"); checkBin("&");
    checkBin(","); checkBin("->");
    REQUIRE(L.next().kind == TokenKind::Assign);
    REQUIRE(L.next().kind == TokenKind::GtGt);
}
