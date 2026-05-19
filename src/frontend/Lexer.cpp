#include "Lexer.h"
#include <cctype>
#include <stdexcept>

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
    try {
        t.intValue = std::stoll(t.text);
    } catch (const std::out_of_range&) {
        return makeError("integer literal out of range", startLine, startCol);
    }
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

    int startLine = line_, startCol = col_;
    auto single = [&](TokenKind k) -> Token {
        Token t; t.kind = k; t.text = std::string(1, c); t.line = startLine; t.column = startCol; advance(); return t;
    };
    auto bin1 = [&](const char* s) -> Token {
        Token t; t.kind = TokenKind::BinaryOp; t.text = s; t.line = startLine; t.column = startCol; advance(); return t;
    };

    switch (c) {
        case '(': return single(TokenKind::LParen);
        case ')': return single(TokenKind::RParen);
        case '[': return single(TokenKind::LBracket);
        case ']': return single(TokenKind::RBracket);
        case '{': return single(TokenKind::LBrace);
        case '}': return single(TokenKind::RBrace);
        case '.': return single(TokenKind::Period);
        case ';': return single(TokenKind::Semicolon);
        case '^': return single(TokenKind::Caret);
        case '|': return single(TokenKind::Pipe);
        case '+': case '*': case '/': case '&':
            return bin1(std::string(1, c).c_str());
        case ',': return bin1(",");
        case '-':
            if (lookahead() == '>') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "->";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("-");
        case '=':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "==";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("=");
        case '~':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "~=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return makeError("unexpected '~'", startLine, startCol);
        case '<':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "<=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("<");
        case '>':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = ">=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            if (lookahead() == '>') {
                Token t; t.kind = TokenKind::GtGt; t.text = ">>";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1(">");
        case ':':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::Assign; t.text = ":=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return single(TokenKind::Colon);
    }

    return makeError(std::string("unexpected character '") + c + "'", line_, col_);
}

const Token& Lexer::peek() {
    if (!hasPeek_) { peekTok_ = next(); hasPeek_ = true; }
    return peekTok_;
}

} // namespace protoST
