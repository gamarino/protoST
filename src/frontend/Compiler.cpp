#include "Compiler.h"

namespace protoST {
using namespace ast;

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
