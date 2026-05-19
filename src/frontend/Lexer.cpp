#include "Lexer.h"
#include <cctype>

namespace protoST {

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

void Lexer::advance() {
    if (pos_ >= source_.size()) return;
    if (source_[pos_] == '\n') { ++line_; col_ = 1; }
    else                       { ++col_; }
    ++pos_;
}

void Lexer::skipWhitespace() {
    while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_]))) {
        advance();
    }
}

Token Lexer::lexIdentifier() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() &&
           (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
        advance();
    }
    Token t;
    t.kind = TokenKind::Identifier;
    t.text = source_.substr(start, pos_ - start);
    t.line = startLine; t.column = startCol;
    return t;
}

Token Lexer::lexNumber() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
        advance();
    }
    Token t;
    t.kind = TokenKind::Integer;
    t.text = source_.substr(start, pos_ - start);
    t.intValue = std::stoll(t.text);
    t.line = startLine; t.column = startCol;
    return t;
}

Token Lexer::makeError(const std::string& msg, int l, int c) {
    Token t; t.kind = TokenKind::Error; t.text = msg; t.line = l; t.column = c; return t;
}

Token Lexer::next() {
    if (hasPeek_) { hasPeek_ = false; return peekTok_; }
    skipWhitespace();
    if (pos_ >= source_.size()) {
        Token t; t.kind = TokenKind::EndOfFile; t.line = line_; t.column = col_; return t;
    }
    char c = current();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return lexIdentifier();
    if (std::isdigit(static_cast<unsigned char>(c)))             return lexNumber();
    return makeError(std::string("unexpected character '") + c + "'", line_, col_);
}

Token Lexer::peek() {
    if (!hasPeek_) { peekTok_ = next(); hasPeek_ = true; }
    return peekTok_;
}

} // namespace protoST
