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
    // D16: parses one element of a `#( … )` literal array. Elements are
    // compile-time literals; a nested `#( … )` or a bare `( … )` group is
    // itself a nested literal array. `closeKind` is the bracket that closes
    // the current array (always RParen) — passed for recursion clarity.
    ast::NodePtr parseLiteralArray(int openLine, int openCol);
    ast::NodePtr parseLiteralArrayElement();
    ast::NodePtr parseClassDecl(Token classIdent);
    ast::NodePtr parseMethodDecl(Token classIdent, bool classSide);
};

} // namespace protoST
