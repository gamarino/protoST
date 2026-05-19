#pragma once
#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace protoST::ast {

enum class NodeKind : uint8_t {
    // Expressions
    IntegerLit, FloatLit, StringLit, SymbolLit, CharLit,
    TrueLit, FalseLit, NilLit,
    ArrayLit,        // #(...)
    DynArrayLit,     // {...}
    Identifier,
    Self, Super, ThisContext,
    Assignment,
    UnarySend, BinarySend, KeywordSend,
    Cascade,
    Block,
    Return,
    // Top-level
    MethodDecl,
    ClassDecl,
    Module,
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NodeKind kind;
    int line = 0;
    int column = 0;

    // Common payloads (only some used per kind; checked by kind).
    std::string text;            // identifier name, selector, raw string
    long long   intValue = 0;
    double      floatValue = 0;
    std::vector<NodePtr> children;
    std::vector<std::string> stringList; // e.g., keyword parts of a Block's args, inst-var names
    bool boolFlag = false;        // e.g., ClassDecl: isClassSide on method, etc.

    explicit Node(NodeKind k) : kind(k) {}
};

// Construction helpers — keep call sites short
inline NodePtr makeNode(NodeKind k, int line, int col) {
    auto n = std::make_unique<Node>(k);
    n->line = line; n->column = col;
    return n;
}

} // namespace protoST::ast
