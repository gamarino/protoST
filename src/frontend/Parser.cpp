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

// Stubs that subsequent tasks fill in
ast::NodePtr Parser::parseTopForm()    { error(current_, "unexpected token at top level"); advance(); return nullptr; }
ast::NodePtr Parser::parseStatement()  { return nullptr; }
ast::NodePtr Parser::parseExpression() { return nullptr; }
ast::NodePtr Parser::parseAssignmentRHS(ast::NodePtr) { return nullptr; }
ast::NodePtr Parser::parseKeywordSend(){ return nullptr; }
ast::NodePtr Parser::parseBinarySend() { return nullptr; }
ast::NodePtr Parser::parseUnarySend()  { return nullptr; }
ast::NodePtr Parser::parsePrimary()    { return nullptr; }
ast::NodePtr Parser::parseBlock()      { return nullptr; }
ast::NodePtr Parser::parseClassDecl(Token)         { return nullptr; }
ast::NodePtr Parser::parseMethodDecl(Token, bool)  { return nullptr; }

} // namespace protoST
