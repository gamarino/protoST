#pragma once
#include <cstdint>
#include <string>

namespace protoST {

enum class TokenKind : uint8_t {
    // Literals
    Integer,            // 42, -7  (negative handled by parser, lexer sees '-')
    Float,              // 3.14
    String,             // 'hola'
    Char,               // $a
    Symbol,             // #foo, #+, #at:put:
    True, False, Nil,

    // Identifiers and selectors
    Identifier,         // counter, value
    Keyword,            // foo:   (identifier + colon, no space)
    BinaryOp,           // +  -  *  /  =  ==  ~=  <  >  <=  >=  &  |  @  ,  ->

    // Punctuation / structural
    LParen, RParen,     // ( )
    LBracket, RBracket, // [ ]
    LBrace, RBrace,     // { }
    HashLParen,         // #(   array literal
    Pipe,               // |    locals separator and binary op (disambiguated by parser)
    Period,             // .    statement terminator
    Semicolon,          // ;    cascade
    Caret,              // ^    return
    Assign,             // :=
    Colon,              // :    block argument prefix
    GtGt,               // >>   method definition marker

    // End-of-stream
    EndOfFile,
    Error,              // sentinel — lexer attaches message in `text`
};

struct Token {
    TokenKind kind;
    std::string text;   // raw source slice (for identifiers, literals, errors)
    long long  intValue = 0;     // valid for TokenKind::Integer
    double     floatValue = 0.0; // valid for TokenKind::Float
    int line = 1;
    int column = 1;
};

inline const char* tokenKindName(TokenKind k) {
    switch (k) {
        case TokenKind::Integer:    return "Integer";
        case TokenKind::Float:      return "Float";
        case TokenKind::String:     return "String";
        case TokenKind::Char:       return "Char";
        case TokenKind::Symbol:     return "Symbol";
        case TokenKind::True:       return "True";
        case TokenKind::False:      return "False";
        case TokenKind::Nil:        return "Nil";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::Keyword:    return "Keyword";
        case TokenKind::BinaryOp:   return "BinaryOp";
        case TokenKind::LParen:     return "LParen";
        case TokenKind::RParen:     return "RParen";
        case TokenKind::LBracket:   return "LBracket";
        case TokenKind::RBracket:   return "RBracket";
        case TokenKind::LBrace:     return "LBrace";
        case TokenKind::RBrace:     return "RBrace";
        case TokenKind::HashLParen: return "HashLParen";
        case TokenKind::Pipe:       return "Pipe";
        case TokenKind::Period:     return "Period";
        case TokenKind::Semicolon:  return "Semicolon";
        case TokenKind::Caret:      return "Caret";
        case TokenKind::Assign:     return "Assign";
        case TokenKind::Colon:      return "Colon";
        case TokenKind::GtGt:       return "GtGt";
        case TokenKind::EndOfFile:  return "EOF";
        case TokenKind::Error:      return "Error";
    }
    return "?";
}

} // namespace protoST
