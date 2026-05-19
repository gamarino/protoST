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
