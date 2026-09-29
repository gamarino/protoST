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
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (std::isspace(static_cast<unsigned char>(c))) { advance(); continue; }
        if (c == '"') {
            const int commentLine = line_, commentCol = col_;
            advance();
            while (pos_ < source_.size() && source_[pos_] != '"') advance();
            if (pos_ < source_.size()) { advance(); continue; }
            // An unclosed comment used to swallow the rest of the file.
            unterminatedComment_ = true;
            unterminatedLine_ = commentLine;
            unterminatedCol_ = commentCol;
            continue;
        }
        break;
    }
}

Token Lexer::lexIdentifier() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() &&
           (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
        advance();
    }
    // Keyword selector: identifier IMMEDIATELY followed by ':' (no whitespace)
    if (pos_ < source_.size() && source_[pos_] == ':' &&
        (pos_ + 1 >= source_.size() || source_[pos_ + 1] != '=')) {
        advance();   // consume ':'
        Token t;
        t.kind = TokenKind::Keyword;
        t.text = source_.substr(start, pos_ - start);
        t.line = startLine; t.column = startCol;
        return t;
    }
    Token t;
    t.kind = TokenKind::Identifier;
    t.text = source_.substr(start, pos_ - start);
    if (t.text == "true")  t.kind = TokenKind::True;
    else if (t.text == "false") t.kind = TokenKind::False;
    else if (t.text == "nil")   t.kind = TokenKind::Nil;
    t.line = startLine; t.column = startCol;
    return t;
}

// D1: a `-` immediately preceding a digit, in operand position, is the sign of
// a negative numeric literal. `next()` decides whether the sign belongs to the
// literal and, if so, has already consumed the `-` before calling here with
// `negative == true`; `startLine`/`startCol` are captured from the `-`.
Token Lexer::lexNumber(bool negative) {
    int startLine = line_, startCol = col_;
    if (negative) startCol -= 1;  // the sign sits one column to the left of the digits
    const std::string sign = negative ? "-" : "";
    auto isDigitIn = [](char c, int base) {
        int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
              : std::isalpha(static_cast<unsigned char>(c))
                    ? std::toupper(static_cast<unsigned char>(c)) - 'A' + 10 : 99;
        return v < base;
    };
    std::string digits;
    while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
        digits += source_[pos_]; advance();
    }
    Token t;
    t.line = startLine; t.column = startCol;

    // Radix literal: <base>r<digits>, e.g. 16rFF, 2r1010 (base 2..36).
    if (pos_ + 1 < source_.size() && source_[pos_] == 'r') {
        int base = 0;
        try { base = std::stoi(digits); } catch (...) { base = 0; }
        if (base >= 2 && base <= 36 && isDigitIn(source_[pos_ + 1], base)) {
            advance(); // r
            std::string rd;
            while (pos_ < source_.size() && isDigitIn(source_[pos_], base)) {
                rd += static_cast<char>(std::toupper(static_cast<unsigned char>(source_[pos_])));
                advance();
            }
            t.kind = TokenKind::Integer;
            t.text = sign + digits + "r" + rd;
            try {
                std::size_t used = 0;
                long long v = std::stoll(rd, &used, base);
                t.intValue = negative ? -v : v;
            } catch (const std::out_of_range&) {
                t.large = true; t.radix = base; t.text = sign + rd;
            }
            return t;
        }
    }

    bool isFloat = false;
    std::string frac;
    if (pos_ < source_.size() && source_[pos_] == '.' &&
        pos_ + 1 < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_ + 1]))) {
        isFloat = true;
        advance(); // .
        while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
            frac += source_[pos_]; advance();
        }
    }

    // Exponent: e<digits> or e-<digits>, immediately after the mantissa.
    std::string exponent;
    if (pos_ + 1 < source_.size() && source_[pos_] == 'e') {
        const bool neg = source_[pos_ + 1] == '-';
        const size_t firstDigit = pos_ + (neg ? 2 : 1);
        if (firstDigit < source_.size() && std::isdigit(static_cast<unsigned char>(source_[firstDigit]))) {
            advance(); if (neg) advance();
            exponent = neg ? "-" : "";
            while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
                exponent += source_[pos_]; advance();
            }
        }
    }

    // An integer mantissa with a non-negative exponent stays an Integer
    // (1e3 = 1000); a fraction or a negative exponent makes a Float.
    if (!isFloat && !exponent.empty() && exponent[0] != '-') {
        int e = 0;
        try { e = std::stoi(exponent); } catch (...) { e = 400; }
        if (e > 400) return makeError("integer literal exponent too large", startLine, startCol);
        digits += std::string(static_cast<size_t>(e), '0');
        exponent.clear();
    }
    if (isFloat || !exponent.empty()) {
        t.kind = TokenKind::Float;
        t.text = sign + digits + (frac.empty() ? "" : "." + frac) + (exponent.empty() ? "" : "e" + exponent);
        try {
            t.floatValue = std::stod(t.text);
        } catch (const std::out_of_range&) {
            return makeError("float literal out of range", startLine, startCol);
        }
        return t;
    }
    t.kind = TokenKind::Integer;
    t.text = sign + digits;
    try {
        t.intValue = std::stoll(t.text);
    } catch (const std::out_of_range&) {
        t.large = true; t.radix = 10;   // a LargeInteger literal
    }
    return t;
}

Token Lexer::lexString() {
    int startLine = line_, startCol = col_;
    advance();  // consume opening '
    std::string out;
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == '\'') {
            // possible escape: '' -> '
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '\'') {
                out += '\'';
                advance(); advance();
                continue;
            }
            advance();
            Token t; t.kind = TokenKind::String; t.text = std::move(out);
            t.line = startLine; t.column = startCol; return t;
        }
        out += c;
        advance();
    }
    return makeError("unterminated string literal", startLine, startCol);
}

Token Lexer::lexChar() {
    int startLine = line_, startCol = col_;
    advance(); // consume '$'
    if (pos_ >= source_.size()) {
        return makeError("unterminated char literal", startLine, startCol);
    }
    Token t; t.kind = TokenKind::Char; t.text = std::string(1, source_[pos_]);
    t.line = startLine; t.column = startCol;
    advance();
    return t;
}

Token Lexer::lexSymbol() {
    int startLine = line_, startCol = col_;
    advance(); // consume '#'
    if (pos_ >= source_.size()) {
        return makeError("incomplete symbol", startLine, startCol);
    }
    char c = source_[pos_];
    Token t; t.kind = TokenKind::Symbol; t.line = startLine; t.column = startCol;

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        // identifier OR keyword-chain
        std::string out;
        while (pos_ < source_.size() &&
               (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
            out += source_[pos_]; advance();
        }
        // chain of `name:` segments
        while (pos_ < source_.size() && source_[pos_] == ':' &&
               (pos_ + 1 >= source_.size() || source_[pos_ + 1] != '=')) {
            out += ':'; advance();
            while (pos_ < source_.size() &&
                   (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
                out += source_[pos_]; advance();
            }
        }
        t.text = std::move(out);
        return t;
    }

    // binary operator symbol: take 1–2 chars from the binop alphabet
    static const std::string binChars = "+-*/=~<>&|@,";
    if (binChars.find(c) != std::string::npos) {
        std::string out; out += c; advance();
        if (pos_ < source_.size() && binChars.find(source_[pos_]) != std::string::npos) {
            out += source_[pos_]; advance();
        }
        t.text = std::move(out);
        return t;
    }
    return makeError("malformed symbol literal", startLine, startCol);
}

Token Lexer::makeError(const std::string& msg, int l, int c) {
    Token t; t.kind = TokenKind::Error; t.text = msg; t.line = l; t.column = c; return t;
}

// D1: tokens after which a `-` must be the binary minus operator. These are
// exactly the tokens that *end an operand* — a literal, an identifier, or a
// closing bracket. In every other position (start of stream, after a binary
// operator, a keyword, `(`, `[`, `^`, `:=`, `.`, `;`, `>>`) a `-` directly
// followed by a digit opens a negative numeric literal.
bool Lexer::prevEndsOperand() const {
    switch (prevReturnedKind_) {
        case TokenKind::Integer:
        case TokenKind::Float:
        case TokenKind::String:
        case TokenKind::Char:
        case TokenKind::Symbol:
        case TokenKind::True:
        case TokenKind::False:
        case TokenKind::Nil:
        case TokenKind::Identifier:
        case TokenKind::RParen:
        case TokenKind::RBracket:
        case TokenKind::RBrace:
            return true;
        default:
            return false;
    }
}

// True when [from, to) -- the whitespace and comments skipped before a token --
// contains a line holding nothing but whitespace. A line whose only content is
// a comment is not blank. The first line of the range continues the previous
// token's line, so it never counts.
bool Lexer::containsBlankLine(size_t from, size_t to) const {
    bool sawNewline = false;
    bool lineHasContent = true;
    bool inComment = false;
    for (size_t i = from; i < to && i < source_.size(); ++i) {
        const char c = source_[i];
        if (c == '"') { inComment = !inComment; lineHasContent = true; continue; }
        if (c == '\n') {
            if (sawNewline && !lineHasContent) return true;
            sawNewline = true;
            lineHasContent = inComment;
            continue;
        }
        if (inComment || !std::isspace(static_cast<unsigned char>(c))) lineHasContent = true;
    }
    return false;
}

Token Lexer::next() {
    if (!lookahead_.empty()) {
        Token t = std::move(lookahead_.front());
        lookahead_.pop_front();
        return t;
    }
    return lexOne();
}

// Lex one token and record what later tokens depend on: the blank-line mark
// and the kind of the last lexed token (D1: sign of a negative literal).
Token Lexer::lexOne() {
    Token t = nextImpl_();
    t.blankLineBefore = blankBefore_;
    prevReturnedKind_ = t.kind;
    return t;
}

Token Lexer::nextImpl_() {
    size_t beforeWs = pos_;
    skipWhitespace();
    // D1: did whitespace (or a comment) separate this token from the previous
    // one? A `-digit` after whitespace is in primary position even when the
    // previous token ends an operand (e.g. each `-2` element of `#(-1 -2 -3)`).
    bool spaceBefore = (pos_ != beforeWs);
    blankBefore_ = containsBlankLine(beforeWs, pos_);
    if (unterminatedComment_) {
        unterminatedComment_ = false;
        return makeError("unterminated comment (opened here)", unterminatedLine_, unterminatedCol_);
    }
    if (pos_ >= source_.size()) {
        Token t; t.kind = TokenKind::EndOfFile; t.line = line_; t.column = col_; return t;
    }
    char c = current();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return lexIdentifier();
    if (std::isdigit(static_cast<unsigned char>(c)))             return lexNumber(false);
    if (c == '#') {
        if (lookahead() == '(') {
            Token t; t.kind = TokenKind::HashLParen; t.text = "#(";
            t.line = line_; t.column = col_; advance(); advance(); return t;
        }
        return lexSymbol();
    }
    if (c == '\'') return lexString();
    if (c == '$')  return lexChar();

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
        case '+': return bin1("+");
        case '*': return bin1("*");
        case '/':
            // D11/D20: `//` is the integer (truncating) division operator of
            // the numeric tower. Two slashes form one binary selector.
            if (lookahead() == '/') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "//";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("/");
        case '&': return bin1("&");
        case ',': return bin1(",");
        case '@': return bin1("@");
        case '-':
            if (lookahead() == '>') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "->";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            // D1: a `-` directly followed by a digit is the sign of a
            // negative numeric literal when it is in primary position —
            // either the previous token does not end an operand, or there is
            // whitespace separating it from that previous token. A `-` glued
            // to an operand (`3-5`, `(x)-5`) stays the binary minus operator,
            // so `a - 5`, `3 - 5`, `3-5` all remain subtraction sends while
            // `-5` and each `-2` inside `#(-1 -2 -3)` lex as literals.
            if (std::isdigit(static_cast<unsigned char>(lookahead())) &&
                (!prevEndsOperand() || spaceBefore)) {
                advance();  // consume the '-'
                return lexNumber(/*negative=*/true);
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
            // D18: `~~` is the identity-inequality binary operator (the mirror
            // of `==`). Two characters from the operator alphabet form one
            // binary operator (§2.10).
            if (lookahead() == '~') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "~~";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return makeError("unexpected '~'", startLine, startCol);
        case '<':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "<=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            // `<<` is a two-character binary operator (the Smalltalk stream
            // append/output selector). Two characters from the operator
            // alphabet form one binary operator (§2.10).
            if (lookahead() == '<') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "<<";
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
        case '\\':
            // D11/D20: `\\` is the modulo (remainder) binary operator of the
            // numeric tower. Two backslashes form one binary selector.
            if (lookahead() == '\\') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "\\\\";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return makeError("unexpected '\\'", startLine, startCol);
    }

    {
        int errLine = line_, errCol = col_;
        advance();  // consume the unknown character so callers don't spin
        return makeError(std::string("unexpected character '") + c + "'", errLine, errCol);
    }
}

const Token& Lexer::peek() {
    if (lookahead_.empty()) lookahead_.push_back(lexOne());
    return lookahead_.front();
}

const Token& Lexer::peekSecond() {
    while (lookahead_.size() < 2) lookahead_.push_back(lexOne());
    return lookahead_[1];
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> result;
    while (true) {
        auto t = next();
        bool eof = (t.kind == TokenKind::EndOfFile);
        result.push_back(std::move(t));
        if (eof) break;
    }
    return result;
}

} // namespace protoST
