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

TEST_CASE("lexer recognizes keyword selectors", "[lexer]") {
    Lexer L("at: put: foo bar:baz");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Keyword);
    REQUIRE(t1.text == "at:");

    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Keyword);
    REQUIRE(t2.text == "put:");

    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::Identifier);
    REQUIRE(t3.text == "foo");

    auto t4 = L.next();
    REQUIRE(t4.kind == TokenKind::Keyword);
    REQUIRE(t4.text == "bar:");

    auto t5 = L.next();
    REQUIRE(t5.kind == TokenKind::Identifier);
    REQUIRE(t5.text == "baz");
}

TEST_CASE("lexer reads single-quoted strings with '' escapes", "[lexer]") {
    Lexer L("'hola' 'it''s ok' ''");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::String);
    REQUIRE(t1.text == "hola");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::String);
    REQUIRE(t2.text == "it's ok");
    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::String);
    REQUIRE(t3.text == "");
}

TEST_CASE("lexer reads char literals $x", "[lexer]") {
    Lexer L("$a $$ $ ");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Char);
    REQUIRE(t1.text == "a");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Char);
    REQUIRE(t2.text == "$");
    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::Char);
    REQUIRE(t3.text == " ");
}

TEST_CASE("lexer reads identifier symbols", "[lexer]") {
    Lexer L("#foo #valueOf");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Symbol);
    REQUIRE(t1.text == "foo");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Symbol);
    REQUIRE(t2.text == "valueOf");
}

TEST_CASE("lexer reads binary symbols", "[lexer]") {
    Lexer L("#+ #<= #==");
    REQUIRE(L.next().text == "+");
    REQUIRE(L.next().text == "<=");
    REQUIRE(L.next().text == "==");
}

TEST_CASE("lexer reads keyword-selector symbols", "[lexer]") {
    Lexer L("#at:put: #foo:");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Symbol);
    REQUIRE(t1.text == "at:put:");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Symbol);
    REQUIRE(t2.text == "foo:");
}

TEST_CASE("lexer skips ST-style \"...\" comments", "[lexer]") {
    Lexer L("\"a comment\" 42 \"another\nmultiline\" foo");
    REQUIRE(L.next().intValue == 42);
    auto t = L.next();
    REQUIRE(t.kind == TokenKind::Identifier);
    REQUIRE(t.text == "foo");
}

TEST_CASE("lexer reads floats", "[lexer]") {
    Lexer L("3.14 0.5 42");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Float);
    REQUIRE(t1.floatValue == Catch::Approx(3.14));
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Float);
    REQUIRE(t2.floatValue == Catch::Approx(0.5));
    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::Integer);
    REQUIRE(t3.intValue == 42);
}
