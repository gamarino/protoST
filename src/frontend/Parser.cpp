#include "Parser.h"

#include <cctype>

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
    // class/method declarations begin with `Identifier`. Distinguish:
    //   Identifier 'subclass:' #Identifier ... .                 -> ClassDecl
    //   Identifier ['class'] '>>' <selector-pattern> body        -> MethodDecl
    //   else                                                     -> expression statement '.'
    if (current_.kind == TokenKind::Identifier) {
        Token classId = current_;
        Token after = lexer_.peek();
        if (after.kind == TokenKind::GtGt) {
            advance(); // consume class id
            advance(); // consume >>
            return parseMethodDecl(classId, /*classSide=*/false);
        }
        if (after.kind == TokenKind::Identifier && after.text == "class") {
            // peek one more - need a 2-token lookahead. Pull both tokens into prev.
            advance(); // class id
            advance(); // 'class'
            if (current_.kind == TokenKind::GtGt) {
                advance(); // >>
                return parseMethodDecl(classId, /*classSide=*/true);
            }
            // not a method decl; bail to expression - rewind impossible, so report error
            error(current_, "expected '>>' after 'class'");
            synchronize();
            return nullptr;
        }
        if (after.kind == TokenKind::Keyword && after.text == "subclass:") {
            advance(); // consume class id
            advance(); // consume 'subclass:'
            return parseClassDecl(classId);
        }
    }
    auto stmt = parseStatement();
    if (!match(TokenKind::Period) && current_.kind != TokenKind::EndOfFile) {
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

ast::NodePtr Parser::parseMethodDecl(Token classIdent, bool classSide) {
    auto md = ast::makeNode(ast::NodeKind::MethodDecl, classIdent.line, classIdent.column);
    md->text = classIdent.text;
    md->boolFlag = classSide;

    std::string selector;
    int nArgs = 0;

    if (current_.kind == TokenKind::Identifier) {
        // unary
        selector = current_.text;
        advance();
    } else if (current_.kind == TokenKind::BinaryOp) {
        selector = current_.text;
        advance();
        if (current_.kind != TokenKind::Identifier) {
            error(current_, "expected argument name after binary selector");
        } else {
            md->stringList.push_back(current_.text);
            advance();
            ++nArgs;
        }
    } else if (current_.kind == TokenKind::Keyword) {
        while (current_.kind == TokenKind::Keyword) {
            selector += current_.text;
            advance();
            if (current_.kind != TokenKind::Identifier) {
                error(current_, "expected argument name after keyword");
                break;
            }
            md->stringList.push_back(current_.text);
            advance();
            ++nArgs;
        }
    } else {
        error(current_, "expected method selector");
    }

    // selector occupies index 0; shift arguments to start at index 1
    md->stringList.insert(md->stringList.begin(), selector);
    md->intValue = nArgs;

    // optional locals
    if (current_.kind == TokenKind::Pipe) {
        advance();
        while (current_.kind == TokenKind::Identifier) {
            md->stringList.push_back(current_.text);
            advance();
        }
        consume(TokenKind::Pipe, "expected '|' to close method locals");
    }

    // body: statements until we see a token that can only start a top-level form
    // (another Identifier followed by '>>' / 'class' / 'subclass:', or EOF).
    while (current_.kind != TokenKind::EndOfFile) {
        // stop at the start of another method/class decl
        if (current_.kind == TokenKind::Identifier) {
            Token p = lexer_.peek();
            if (p.kind == TokenKind::GtGt) break;
            if (p.kind == TokenKind::Identifier && p.text == "class") break;
            if (p.kind == TokenKind::Keyword && p.text == "subclass:") break;
        }
        auto stmt = parseStatement();
        if (stmt) md->children.push_back(std::move(stmt));
        if (!match(TokenKind::Period)) break;
    }
    return md;
}

// Stubs that subsequent tasks fill in
ast::NodePtr Parser::parseAssignmentRHS(ast::NodePtr) { return nullptr; }

ast::NodePtr Parser::parseClassDecl(Token classIdent) {
    auto cd = ast::makeNode(ast::NodeKind::ClassDecl, classIdent.line, classIdent.column);
    cd->stringList.push_back(classIdent.text);   // superclass name at [0]

    // expect a Symbol literal #Name
    if (current_.kind != TokenKind::Symbol) {
        error(current_, "expected #ClassName after subclass:");
        synchronize();
        return cd;
    }
    cd->text = current_.text;
    advance();

    auto parseStringList = [&](std::vector<std::string>& out) {
        // parses a single 'a b c' string literal as space-separated identifiers
        if (current_.kind == TokenKind::String) {
            std::string s = current_.text;
            advance();
            std::string cur;
            for (char ch : s) {
                if (std::isspace(static_cast<unsigned char>(ch))) {
                    if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); }
                } else cur += ch;
            }
            if (!cur.empty()) out.push_back(std::move(cur));
        } else {
            error(current_, "expected string literal");
        }
    };

    while (current_.kind == TokenKind::Keyword) {
        if (current_.text == "instanceVariableNames:") {
            advance();
            parseStringList(cd->stringList);
        } else if (current_.text == "classVariableNames:") {
            advance();
            // ignore class-var contents for now; future MetaclassDecl pulls them out
            if (current_.kind == TokenKind::String) advance();
            else error(current_, "expected string after classVariableNames:");
        } else {
            error(current_, "unknown keyword in class declaration: " + current_.text);
            break;
        }
    }

    // consume optional terminating '.'
    match(TokenKind::Period);
    return cd;
}

} // namespace protoST
