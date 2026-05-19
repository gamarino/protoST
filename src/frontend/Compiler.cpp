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
    (void)n;
    error("expression kind not yet supported in compiler scaffold");
    m.emit(Op::PUSH_NIL, 0);
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
