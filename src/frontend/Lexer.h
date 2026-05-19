#pragma once
#include "Token.h"
#include <string>
#include <string_view>
#include <vector>

namespace protoST {

class Lexer {
public:
    explicit Lexer(std::string source);
    Token next();
    const Token& peek();
    bool  atEnd() const { return pos_ >= source_.size(); }
    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t pos_ = 0;
    int    line_ = 1;
    int    col_ = 1;
    bool   hasPeek_ = false;
    Token  peekTok_;

    void   advance();
    char   current() const { return pos_ < source_.size() ? source_[pos_] : '\0'; }
    char   lookahead(size_t k = 1) const {
        return (pos_ + k) < source_.size() ? source_[pos_ + k] : '\0';
    }
    void   skipWhitespace();
    Token  lexIdentifier();
    Token  lexNumber();
    Token  lexString();
    Token  lexChar();
    Token  lexSymbol();
    Token  makeError(const std::string& msg, int l, int c);
};

} // namespace protoST
