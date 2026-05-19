#include "Parser.h"

namespace protoST {

Parser::Parser(std::string source) : lexer_(std::move(source)) {
    advance();
}

void Parser::advance() {
    prev_ = current_;
    current_ = lexer_.next();
    while (current_.kind == TokenKind::Error) {
        error(current_, current_.text);
        current_ = lexer_.next();
    }
}

bool Parser::match(TokenKind k) {
    if (current_.kind != k) return false;
    advance();
    return true;
}

Token Parser::consume(TokenKind k, const std::string& msg) {
    if (current_.kind == k) { Token t = current_; advance(); return t; }
    error(current_, msg);
    return current_;
}

void Parser::error(const Token& at, const std::string& msg) {
    errors_.push_back(ParseError{msg, at.line, at.column});
}

void Parser::synchronize() {
    while (current_.kind != TokenKind::EndOfFile &&
           current_.kind != TokenKind::Period) {
        advance();
    }
    if (current_.kind == TokenKind::Period) advance();
}

ast::NodePtr Parser::parseModule() {
    auto mod = ast::makeNode(ast::NodeKind::Module, 1, 1);
    while (current_.kind != TokenKind::EndOfFile) {
        auto top = parseTopForm();
        if (top) mod->children.push_back(std::move(top));
        else     synchronize();
    }
    return mod;
}

ast::NodePtr Parser::parseTopForm() {
    auto stmt = parseStatement();
    if (!match(TokenKind::Period)) {
        // implicit period at EOF is allowed
        if (current_.kind != TokenKind::EndOfFile)
            error(current_, "expected '.' after top-level expression");
    }
    return stmt;
}

ast::NodePtr Parser::parseStatement() {
    if (match(TokenKind::Caret)) {
        auto n = ast::makeNode(ast::NodeKind::Return, prev_.line, prev_.column);
        auto inner = parseExpression();
        if (inner) n->children.push_back(std::move(inner));
        return n;
    }
    return parseExpression();
}

ast::NodePtr Parser::parseExpression() {
    return parseKeywordSend();
}

ast::NodePtr Parser::parseKeywordSend() { return parseBinarySend(); }
ast::NodePtr Parser::parseBinarySend()  { return parseUnarySend(); }
ast::NodePtr Parser::parseUnarySend()   { return parsePrimary(); }

ast::NodePtr Parser::parsePrimary() {
    Token t = current_;
    switch (current_.kind) {
        case TokenKind::Integer: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::IntegerLit, t.line, t.column);
            n->intValue = t.intValue; n->text = t.text;
            return n;
        }
        case TokenKind::Float: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::FloatLit, t.line, t.column);
            n->floatValue = t.floatValue; n->text = t.text;
            return n;
        }
        case TokenKind::String: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::StringLit, t.line, t.column);
            n->text = t.text; return n;
        }
        case TokenKind::Char: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::CharLit, t.line, t.column);
            n->text = t.text; return n;
        }
        case TokenKind::Symbol: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::SymbolLit, t.line, t.column);
            n->text = t.text; return n;
        }
        case TokenKind::True:   advance(); return ast::makeNode(ast::NodeKind::TrueLit,  t.line, t.column);
        case TokenKind::False:  advance(); return ast::makeNode(ast::NodeKind::FalseLit, t.line, t.column);
        case TokenKind::Nil:    advance(); return ast::makeNode(ast::NodeKind::NilLit,   t.line, t.column);
        case TokenKind::Identifier: {
            advance();
            ast::NodeKind k = ast::NodeKind::Identifier;
            if (t.text == "self")        k = ast::NodeKind::Self;
            else if (t.text == "super")  k = ast::NodeKind::Super;
            else if (t.text == "thisContext") k = ast::NodeKind::ThisContext;
            auto n = ast::makeNode(k, t.line, t.column);
            n->text = t.text;
            return n;
        }
        case TokenKind::LParen: {
            advance();
            auto inner = parseExpression();
            consume(TokenKind::RParen, "expected ')'");
            return inner;
        }
        default:
            error(current_, std::string("expected primary expression, got ") + tokenKindName(current_.kind));
            advance();
            return nullptr;
    }
}

// Stubs that subsequent tasks fill in
ast::NodePtr Parser::parseAssignmentRHS(ast::NodePtr) { return nullptr; }
ast::NodePtr Parser::parseBlock()      { return nullptr; }
ast::NodePtr Parser::parseClassDecl(Token)         { return nullptr; }
ast::NodePtr Parser::parseMethodDecl(Token, bool)  { return nullptr; }

} // namespace protoST
