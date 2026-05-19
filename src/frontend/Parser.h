#pragma once
#include "AST.h"
#include "Lexer.h"
#include <string>
#include <vector>

namespace protoST {

struct ParseError {
    std::string message;
    int line = 0;
    int column = 0;
};

class Parser {
public:
    explicit Parser(std::string source);

    ast::NodePtr parseModule();
    const std::vector<ParseError>& errors() const { return errors_; }

private:
    Lexer  lexer_;
    Token  current_;
    Token  prev_;
    std::vector<ParseError> errors_;

    void   advance();
    bool   check(TokenKind k) const { return current_.kind == k; }
    bool   match(TokenKind k);
    Token  consume(TokenKind k, const std::string& msg);
    void   error(const Token& at, const std::string& msg);
    void   synchronize();

    // grammar entry points (added in later tasks)
    ast::NodePtr parseTopForm();
    ast::NodePtr parseStatement();
    ast::NodePtr parseExpression();
    ast::NodePtr parseAssignmentRHS(ast::NodePtr target);
    ast::NodePtr parseKeywordSend();
    ast::NodePtr parseBinarySend();
    ast::NodePtr parseUnarySend();
    ast::NodePtr parsePrimary();
    ast::NodePtr parseBlock();
    ast::NodePtr parseClassDecl(Token classIdent);
    ast::NodePtr parseMethodDecl(Token classIdent, bool classSide);
};

} // namespace protoST
