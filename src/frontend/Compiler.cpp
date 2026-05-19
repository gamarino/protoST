#include "Compiler.h"

namespace protoST {
using namespace ast;

namespace {

// One walker per scope (module, method, or block). Accumulates:
//   - declared: names introduced in THIS scope (module-level assignments,
//               method args/locals, block args/locals)
//   - directRefs: identifier references that occur directly in this scope's
//                 statements (not recursing into nested blocks)
//   - innerNeeds: union of free variables (referencedHere - declaredThere) of
//                 each immediately-nested Block. These are the names inner
//                 blocks expect to find in some enclosing scope. The subset
//                 of declared ∩ innerNeeds is what this scope must keep in
//                 its captured dict.
struct ScopeWalker {
    std::unordered_set<std::string> declared;
    std::unordered_set<std::string> directRefs;
    std::unordered_set<std::string> innerNeeds;
};

// Forward declarations.
void walkNode(const Node& n, ScopeWalker& cur,
              Compiler::ScopeAnalysis& out, const Node* scopeKey);
void walkBlockBody(const Node& blockNode, ScopeWalker& blockScope,
                   Compiler::ScopeAnalysis& out);

// Compute the free variables a scope exposes to its parent:
//   (directRefs ∪ innerNeeds) − declared.
std::unordered_set<std::string> freeVarsOf(const ScopeWalker& sw) {
    std::unordered_set<std::string> out;
    out.reserve(sw.directRefs.size() + sw.innerNeeds.size());
    for (const auto& r : sw.directRefs) {
        if (sw.declared.count(r) == 0) out.insert(r);
    }
    for (const auto& r : sw.innerNeeds) {
        if (sw.declared.count(r) == 0) out.insert(r);
    }
    return out;
}

// Compute the captured set of a scope:
//   declared ∩ innerNeeds. These are this scope's bindings that some inner
//   block referenced and that therefore must live in the captured dict.
std::unordered_set<std::string> capturedOf(const ScopeWalker& sw) {
    std::unordered_set<std::string> out;
    for (const auto& n : sw.declared) {
        if (sw.innerNeeds.count(n) != 0) out.insert(n);
    }
    return out;
}

// Walk a single AST node, updating the current scope walker. When a nested
// Block is encountered, a fresh ScopeWalker is opened, the block body is
// processed, and the block's free vars are added to the parent's innerNeeds.
// MethodDecl is treated as a scope boundary as well — inner blocks of a
// method capture from the method's scope, not from the module's.
void walkNode(const Node& n, ScopeWalker& cur,
              Compiler::ScopeAnalysis& out, const Node* /*scopeKey*/) {
    switch (n.kind) {
        case NodeKind::Identifier:
            // Self/Super/ThisContext are separate NodeKinds and never reach here.
            if (!n.text.empty()) cur.directRefs.insert(n.text);
            return;

        case NodeKind::Self:
        case NodeKind::Super:
        case NodeKind::ThisContext:
            // Pseudo-variables; not regular identifier references.
            return;

        case NodeKind::Assignment: {
            // The LHS name is declared in this scope; the RHS expression
            // (children[0]) is walked normally.
            if (!n.text.empty()) cur.declared.insert(n.text);
            if (!n.children.empty()) walkNode(*n.children[0], cur, out, nullptr);
            return;
        }

        case NodeKind::Block: {
            // Open a fresh scope for the block.
            ScopeWalker blockScope;
            // n.stringList holds: nArgs args followed by locals.
            for (const auto& name : n.stringList) {
                blockScope.declared.insert(name);
            }
            walkBlockBody(n, blockScope, out);
            // Record this block's captured set under its node pointer.
            out.capturedByScope[&n] = capturedOf(blockScope);
            // Bubble the block's free vars up to the enclosing scope's innerNeeds.
            auto freeVars = freeVarsOf(blockScope);
            for (const auto& f : freeVars) cur.innerNeeds.insert(f);
            return;
        }

        case NodeKind::MethodDecl: {
            // A method is a scope boundary; treat like a block for analysis
            // purposes, but it does NOT bubble free vars up to the module.
            // n.stringList[0] = selector (skip); the rest are args+locals.
            ScopeWalker methodScope;
            for (size_t i = 1; i < n.stringList.size(); ++i) {
                methodScope.declared.insert(n.stringList[i]);
            }
            for (const auto& child : n.children) {
                walkNode(*child, methodScope, out, &n);
            }
            out.capturedByScope[&n] = capturedOf(methodScope);
            // Intentionally do not propagate to cur — method bodies aren't
            // executed inline at module scope.
            return;
        }

        default:
            // Generic compound node — recurse into children. Also walk any
            // stringList? No: stringList is structural (selector, var names),
            // not expressions, so children alone are correct here.
            for (const auto& child : n.children) {
                if (child) walkNode(*child, cur, out, nullptr);
            }
            return;
    }
}

void walkBlockBody(const Node& blockNode, ScopeWalker& blockScope,
                   Compiler::ScopeAnalysis& out) {
    for (const auto& child : blockNode.children) {
        if (child) walkNode(*child, blockScope, out, &blockNode);
    }
}

} // namespace

void Compiler::analyseClosures(const Node& mod) {
    analysis_ = ScopeAnalysis{};
    ScopeWalker moduleScope;
    // Module-level: walk every statement under the module node.
    for (const auto& child : mod.children) {
        if (child) walkNode(*child, moduleScope, analysis_, nullptr);
    }
    analysis_.moduleCaptured = capturedOf(moduleScope);
    // Also expose the module-level captured set under the nullptr key
    // (matches the documented contract on ScopeAnalysis::capturedByScope
    // for the module scope).
    analysis_.capturedByScope[nullptr] = analysis_.moduleCaptured;
}

std::unique_ptr<BytecodeModule> Compiler::compileModule(const Node& mod) {
    auto bc = std::make_unique<BytecodeModule>();
    scopes_.clear();
    scopes_.emplace_back();
    if (mod.children.empty()) {
        bc->emit(Op::PUSH_NIL, 0);
    } else {
        for (size_t i = 0; i < mod.children.size(); ++i) {
            emitStatement(*bc, *mod.children[i]);
            // For all but the last statement, discard the result.
            if (i + 1 != mod.children.size()) bc->emit(Op::POP, 0);
        }
    }
    bc->emit(Op::RETURN_TOP, 0);
    return bc;
}

void Compiler::emitStatement(BytecodeModule& m, const Node& n) {
    if (n.kind == NodeKind::Return) {
        if (!n.children.empty()) emitExpr(m, *n.children[0]);
        else                     m.emit(Op::PUSH_NIL, 0);
        m.emit(Op::RETURN, 0);
        return;
    }
    if (n.kind == NodeKind::Assignment) {
        // Statement-level assignment: declare slot, evaluate RHS, DUP so STORE_LOCAL
        // (which pops) leaves the assigned value on the stack for the top-level POP
        // separator (or RETURN_TOP, when it is the last statement).
        int slot = declareLocal(n.text);
        emitExpr(m, *n.children[0]);
        m.emit(Op::DUP, 0);
        m.emit(Op::STORE_LOCAL, static_cast<uint8_t>(slot));
        return;
    }
    emitExpr(m, n);
}

void Compiler::emitExpr(BytecodeModule& m, const Node& n) {
    switch (n.kind) {
        case NodeKind::IntegerLit: {
            auto idx = m.addInteger(n.intValue);
            if (idx > 255) { error("integer constant pool overflow > 255 (EXTEND not yet emitted in F2)"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::FloatLit: {
            auto idx = m.addFloat(n.floatValue);
            if (idx > 255) { error("float constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::StringLit: {
            auto idx = m.addString(n.text);
            if (idx > 255) { error("string constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::SymbolLit: {
            auto idx = m.internSymbol(n.text);
            if (idx > 255) { error("symbol constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::CharLit: {
            auto idx = m.addChar(n.text);
            if (idx > 255) { error("char constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::TrueLit:  m.emit(Op::PUSH_TRUE, 0); return;
        case NodeKind::FalseLit: m.emit(Op::PUSH_FALSE, 0); return;
        case NodeKind::NilLit:   m.emit(Op::PUSH_NIL, 0); return;
        case NodeKind::Self:     m.emit(Op::PUSH_SELF, 0); return;
        case NodeKind::Super:    m.emit(Op::PUSH_SUPER, 0); return;
        case NodeKind::Identifier: {
            int slot = resolveLocal(n.text);
            if (slot < 0) {
                error("unknown identifier '" + n.text + "'");
                m.emit(Op::PUSH_NIL, 0);
                return;
            }
            m.emit(Op::PUSH_LOCAL, static_cast<uint8_t>(slot));
            return;
        }
        case NodeKind::Assignment: {
            // Expression-position assignment: declare slot, evaluate RHS, then DUP so
            // STORE_LOCAL (which pops) leaves the assigned value on the stack as the
            // expression's result.
            int slot = declareLocal(n.text);
            emitExpr(m, *n.children[0]);
            m.emit(Op::DUP, 0);
            m.emit(Op::STORE_LOCAL, static_cast<uint8_t>(slot));
            return;
        }
        case NodeKind::UnarySend: {
            emitExpr(m, *n.children[0]);
            auto sym = m.internSymbol(n.text);
            if (sym > 255) { error("selector pool overflow"); return; }
            m.emit(Op::SEND_UNARY, static_cast<uint8_t>(sym));
            return;
        }
        case NodeKind::BinarySend: {
            // receiver, then arg, then SEND_BINARY
            emitExpr(m, *n.children[0]);
            if (n.children.size() < 2) { error("binary send missing argument"); return; }
            emitExpr(m, *n.children[1]);
            auto sym = m.internSymbol(n.text);
            if (sym > 255) { error("selector pool overflow"); return; }
            m.emit(Op::SEND_BINARY, static_cast<uint8_t>(sym));
            return;
        }
        case NodeKind::KeywordSend: {
            // receiver, then each arg in order, then SEND_KEYWORD
            emitExpr(m, *n.children[0]);
            for (size_t i = 1; i < n.children.size(); ++i) {
                emitExpr(m, *n.children[i]);
            }
            auto sym = m.internSymbol(n.text);
            if (sym > 255) { error("selector pool overflow"); return; }
            m.emit(Op::SEND_KEYWORD, static_cast<uint8_t>(sym));
            return;
        }
        case NodeKind::Cascade: {
            // children[0] is the receiver; children[1..] are partial sends.
            emitExpr(m, *n.children[0]);
            int tmp = declareLocal("__cascade_tmp_" + std::to_string(scopes_.back().nextSlot));
            for (size_t i = 1; i < n.children.size(); ++i) {
                auto& msg = *n.children[i];
                // For all but the last partial, duplicate receiver before evaluating the send,
                // then POP the result. For the last partial we keep the result.
                bool isLast = (i + 1 == n.children.size());
                m.emit(Op::DUP, 0); // duplicate receiver for this send
                // emit args; receiver is already on stack just below the args
                if (msg.kind == NodeKind::UnarySend) {
                    auto sym = m.internSymbol(msg.text);
                    m.emit(Op::SEND_UNARY, static_cast<uint8_t>(sym));
                } else if (msg.kind == NodeKind::BinarySend) {
                    if (msg.children.empty()) { error("cascade binary missing arg"); }
                    else emitExpr(m, *msg.children[0]);
                    auto sym = m.internSymbol(msg.text);
                    m.emit(Op::SEND_BINARY, static_cast<uint8_t>(sym));
                } else if (msg.kind == NodeKind::KeywordSend) {
                    for (auto& a : msg.children) emitExpr(m, *a);
                    auto sym = m.internSymbol(msg.text);
                    m.emit(Op::SEND_KEYWORD, static_cast<uint8_t>(sym));
                } else {
                    error("unexpected node in cascade");
                }
                if (isLast) {
                    // stack: [receiver, last_result]; stash result, pop receiver, push result back
                    m.emit(Op::STORE_LOCAL, static_cast<uint8_t>(tmp));
                    m.emit(Op::POP, 0);            // discard receiver
                    m.emit(Op::PUSH_LOCAL, static_cast<uint8_t>(tmp));
                } else {
                    m.emit(Op::POP, 0);            // discard this result, keep receiver
                }
            }
            return;
        }
        case NodeKind::Block: {
            auto sub = std::make_unique<BytecodeModule>();
            // open fresh scope for block
            scopes_.emplace_back();
            int nArgs = static_cast<int>(n.intValue);
            sub->setArgCount(nArgs);
            // declare args first (slots 0..nArgs-1), then locals
            for (size_t i = 0; i < n.stringList.size(); ++i) {
                declareLocal(n.stringList[i]);
            }
            // emit body statements; last value implicitly returned
            if (n.children.empty()) sub->emit(Op::PUSH_NIL, 0);
            for (size_t i = 0; i < n.children.size(); ++i) {
                emitStatement(*sub, *n.children[i]);
                if (i + 1 != n.children.size()) sub->emit(Op::POP, 0);
            }
            sub->emit(Op::RETURN_TOP, 0);
            scopes_.pop_back();
            size_t blkIdx = m.addBlockModule(std::move(sub));
            if (blkIdx > 255) { error("block index pool overflow"); return; }
            m.emit(Op::PUSH_BLOCK, static_cast<uint8_t>(blkIdx));
            return;
        }
        default:
            error("expression kind not yet supported");
            m.emit(Op::PUSH_NIL, 0);
            return;
    }
}

int Compiler::declareLocal(const std::string& name) {
    auto& s = scopes_.back();
    auto it = s.slots.find(name);
    if (it != s.slots.end()) return it->second;
    int slot = s.nextSlot++;
    s.slots[name] = slot;
    return slot;
}

int Compiler::resolveLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->slots.find(name);
        if (f != it->slots.end()) return f->second;
    }
    return -1;
}

void Compiler::error(const std::string& msg) { errors_.push_back(msg); }

} // namespace protoST
