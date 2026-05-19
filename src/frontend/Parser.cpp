#include "Parser.h"

namespace protoST {

static bool isSendKind(ast::NodeKind k) {
    return k == ast::NodeKind::UnarySend
        || k == ast::NodeKind::BinarySend
        || k == ast::NodeKind::KeywordSend;
}

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
    if (current_.kind == TokenKind::Identifier && lexer_.peek().kind == TokenKind::Assign) {
        Token id = current_; advance();   // identifier
        advance();                         // ':='
        auto rhs = parseExpression();
        auto n = ast::makeNode(ast::NodeKind::Assignment, id.line, id.column);
        n->text = id.text;
        if (rhs) n->children.push_back(std::move(rhs));
        return n;
    }
    return parseExpression();
}

ast::NodePtr Parser::parseExpression() {
    auto first = parseKeywordSend();
    if (!first || current_.kind != TokenKind::Semicolon || !isSendKind(first->kind)) {
        return first;
    }
    // Promote receiver: cascade.children[0] = first.children[0]; rest are headless sends
    auto cascade = ast::makeNode(ast::NodeKind::Cascade, first->line, first->column);
    cascade->children.push_back(std::move(first->children[0]));
    first->children.erase(first->children.begin());
    cascade->children.push_back(std::move(first));
    while (match(TokenKind::Semicolon)) {
        // parse a single message with receiver=nullptr (we manufacture)
        Token t = current_;
        if (current_.kind == TokenKind::Identifier) {
            // unary selector
            advance();
            auto n = ast::makeNode(ast::NodeKind::UnarySend, t.line, t.column);
            n->text = t.text;
            // chain unary
            while (current_.kind == TokenKind::Identifier) {
                Token chained = current_; advance();
                auto outer = ast::makeNode(ast::NodeKind::UnarySend, chained.line, chained.column);
                outer->text = chained.text;
                outer->children.push_back(std::move(n));
                n = std::move(outer);
            }
            cascade->children.push_back(std::move(n));
        } else if (current_.kind == TokenKind::BinaryOp) {
            Token op = current_; advance();
            auto right = parseUnarySend();
            auto n = ast::makeNode(ast::NodeKind::BinarySend, op.line, op.column);
            n->text = op.text;
            // partial: no receiver (placeholder will be filled at codegen time)
            if (right) n->children.push_back(std::move(right));
            cascade->children.push_back(std::move(n));
        } else if (current_.kind == TokenKind::Keyword) {
            auto n = ast::makeNode(ast::NodeKind::KeywordSend, current_.line, current_.column);
            std::string selector;
            while (current_.kind == TokenKind::Keyword) {
                selector += current_.text; advance();
                auto arg = parseBinarySend();
                if (arg) n->children.push_back(std::move(arg));
            }
            n->text = std::move(selector);
            cascade->children.push_back(std::move(n));
        } else {
            error(current_, "expected message after ';' in cascade");
            break;
        }
    }
    return cascade;
}

ast::NodePtr Parser::parseUnarySend() {
    auto recv = parsePrimary();
    while (recv && current_.kind == TokenKind::Identifier) {
        // distinguish: only an identifier that is NOT followed by ':' is a unary selector;
        // keyword selectors come tokenised as TokenKind::Keyword.
        Token sel = current_;
        advance();
        auto n = ast::makeNode(ast::NodeKind::UnarySend, sel.line, sel.column);
        n->text = sel.text;
        n->children.push_back(std::move(recv));
        recv = std::move(n);
    }
    return recv;
}

ast::NodePtr Parser::parseBinarySend() {
    auto left = parseUnarySend();
    while (left && current_.kind == TokenKind::BinaryOp) {
        Token op = current_; advance();
        auto right = parseUnarySend();
        auto n = ast::makeNode(ast::NodeKind::BinarySend, op.line, op.column);
        n->text = op.text;
        n->children.push_back(std::move(left));
        if (right) n->children.push_back(std::move(right));
        left = std::move(n);
    }
    return left;
}

ast::NodePtr Parser::parseKeywordSend() {
    auto recv = parseBinarySend();
    if (recv && current_.kind == TokenKind::Keyword) {
        auto n = ast::makeNode(ast::NodeKind::KeywordSend, current_.line, current_.column);
        n->children.push_back(std::move(recv));
        std::string selector;
        while (current_.kind == TokenKind::Keyword) {
            selector += current_.text;            // includes trailing ':'
            advance();
            auto arg = parseBinarySend();
            if (arg) n->children.push_back(std::move(arg));
        }
        n->text = std::move(selector);
        return n;
    }
    return recv;
}

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
        case TokenKind::LBracket:
            return parseBlock();
        case TokenKind::LBrace: {
            Token open = current_; advance();
            auto arr = ast::makeNode(ast::NodeKind::DynArrayLit, open.line, open.column);
            while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::EndOfFile) {
                auto e = parseExpression();
                if (e) arr->children.push_back(std::move(e));
                if (!match(TokenKind::Period)) break;
            }
            consume(TokenKind::RBrace, "expected '}' to close dynamic array");
            return arr;
        }
        case TokenKind::HashLParen: {
            Token open = current_; advance();
            auto arr = ast::makeNode(ast::NodeKind::ArrayLit, open.line, open.column);
            while (current_.kind != TokenKind::RParen && current_.kind != TokenKind::EndOfFile) {
                // only literals inside a frozen array
                Token t = current_;
                ast::NodePtr lit;
                if (t.kind == TokenKind::Integer) { advance(); lit = ast::makeNode(ast::NodeKind::IntegerLit, t.line, t.column); lit->intValue = t.intValue; }
                else if (t.kind == TokenKind::Float) { advance(); lit = ast::makeNode(ast::NodeKind::FloatLit, t.line, t.column); lit->floatValue = t.floatValue; }
                else if (t.kind == TokenKind::String) { advance(); lit = ast::makeNode(ast::NodeKind::StringLit, t.line, t.column); lit->text = t.text; }
                else if (t.kind == TokenKind::Symbol) { advance(); lit = ast::makeNode(ast::NodeKind::SymbolLit, t.line, t.column); lit->text = t.text; }
                else if (t.kind == TokenKind::Identifier) { advance(); lit = ast::makeNode(ast::NodeKind::SymbolLit, t.line, t.column); lit->text = t.text; } // bare ids inside #(..) are symbols
                else { error(current_, "unexpected token in frozen array literal"); advance(); continue; }
                arr->children.push_back(std::move(lit));
            }
            consume(TokenKind::RParen, "expected ')' to close frozen array");
            return arr;
        }
        default:
            error(current_, std::string("expected primary expression, got ") + tokenKindName(current_.kind));
            advance();
            return nullptr;
    }
}

ast::NodePtr Parser::parseBlock() {
    Token open = current_; advance(); // consume '['
    auto blk = ast::makeNode(ast::NodeKind::Block, open.line, open.column);

    // arguments: : name : name ... (then a '|' if any arg present)
    int nArgs = 0;
    while (current_.kind == TokenKind::Colon) {
        advance();
        if (current_.kind != TokenKind::Identifier) {
            error(current_, "expected block argument name after ':'");
            break;
        }
        blk->stringList.push_back(current_.text);
        ++nArgs;
        advance();
    }
    if (nArgs > 0) consume(TokenKind::Pipe, "expected '|' after block arguments");

    // locals between '|...|' (if first token is Pipe)
    if (current_.kind == TokenKind::Pipe) {
        advance();
        while (current_.kind == TokenKind::Identifier) {
            blk->stringList.push_back(current_.text);
            advance();
        }
        consume(TokenKind::Pipe, "expected '|' to close block locals");
    }

    blk->intValue = nArgs;

    // statements
    while (current_.kind != TokenKind::RBracket &&
           current_.kind != TokenKind::EndOfFile) {
        auto stmt = parseStatement();
        if (stmt) blk->children.push_back(std::move(stmt));
        if (!match(TokenKind::Period)) break;
    }
    consume(TokenKind::RBracket, "expected ']' to close block");
    return blk;
}

// Stubs that subsequent tasks fill in
ast::NodePtr Parser::parseAssignmentRHS(ast::NodePtr) { return nullptr; }
ast::NodePtr Parser::parseClassDecl(Token)         { return nullptr; }
ast::NodePtr Parser::parseMethodDecl(Token, bool)  { return nullptr; }

} // namespace protoST
