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
    // D1: kind of the last token *returned* to the consumer. Drives the
    // standard Smalltalk disambiguation of a leading `-` — it is part of a
    // negative numeric literal only when the lexer is in operand/primary
    // position (the previous token does not itself end an operand).
    TokenKind prevReturnedKind_ = TokenKind::EndOfFile;

    void   advance();
    // True when the previous returned token ends an operand, so a following
    // `-` must be read as the binary minus operator rather than a literal sign.
    bool   prevEndsOperand() const;
    Token  lexNumber(bool negative);
    // The real tokeniser; `next()` wraps it to record `prevReturnedKind_`.
    Token  nextImpl_();
    char   current() const { return pos_ < source_.size() ? source_[pos_] : '\0'; }
    char   lookahead(size_t k = 1) const {
        return (pos_ + k) < source_.size() ? source_[pos_ + k] : '\0';
    }
    void   skipWhitespace();
    Token  lexIdentifier();
    Token  lexString();
    Token  lexChar();
    Token  lexSymbol();
    Token  makeError(const std::string& msg, int l, int c);
};

} // namespace protoST
