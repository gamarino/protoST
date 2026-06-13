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
    // Call-form send (positional + named args): `recv name(p1, p2, k=v)`
    // Layout: children[0]=receiver, children[1..1+nPos]=positional values,
    // children[1+nPos..1+nPos+nNamed]=named values (sorted-key order);
    // text=method name, intValue=nPos, intValue2=nNamed,
    // boolFlag=true when receiver was implicit (synthesised Self);
    // stringList[0..nNamed-1]=named-arg keys (sorted).
    CallSend,
    Cascade,
    Block,
    Return,
    // Top-level
    MethodDecl,
    // Call-form method declaration: `Class >> name(pos, named=default)`.
    // Layout: text=class name; boolFlag=classSide; intValue=nPos;
    // intValue2=nNamed; stringList[0]=method name,
    // stringList[1..1+nPos]=positional param names,
    // stringList[1+nPos..1+nPos+nNamed]=named param names (sorted),
    // stringList[1+nPos+nNamed..]=user locals;
    // children[0..nNamed-1]=default expressions (sorted-key order),
    // children[nNamed..]=method body statements.
    CallMethodDecl,
    // Class declaration: `Super subclass: #Name instanceVariableNames: '...'
    // classVariableNames: '...' uses: { … }`. Layout:
    //   text                                  = class name (without #)
    //   stringList[0]                         = superclass name
    //   intValue                              = inst-var count
    //   stringList[1..1+intValue]             = inst-var names (source order)
    //   stringList[1+intValue..]              = class-var names (source order)
    //   children[0]                           = optional mixin expression
    //                                           (only present when `uses:`)
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
    // Secondary integer payload. Currently used by CallSend/CallMethodDecl
    // for nNamed (companion to intValue=nPos). Kept zero on other node kinds.
    long long   intValue2 = 0;
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
