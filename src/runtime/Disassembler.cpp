// Disassembler: see Disassembler.h for the listing format.
#include "Disassembler.h"
#include "BytecodeModule.h"
#include "Opcodes.h"

#include <cstdio>
#include <string>
#include <vector>

namespace protoST {

namespace {

const char* opName(Op op) {
    switch (op) {
        case Op::NOP:                return "NOP";
        case Op::PUSH_CONST:         return "PUSH_CONST";
        case Op::PUSH_LOCAL:         return "PUSH_LOCAL";
        case Op::STORE_LOCAL:        return "STORE_LOCAL";
        case Op::DUP:                return "DUP";
        case Op::POP:                return "POP";
        case Op::PUSH_SELF:          return "PUSH_SELF";
        case Op::PUSH_SUPER:         return "PUSH_SUPER";
        case Op::PUSH_NIL:           return "PUSH_NIL";
        case Op::PUSH_TRUE:          return "PUSH_TRUE";
        case Op::PUSH_FALSE:         return "PUSH_FALSE";
        case Op::SEND_UNARY:         return "SEND_UNARY";
        case Op::SEND_BINARY:        return "SEND_BINARY";
        case Op::SEND_KEYWORD:       return "SEND_KEYWORD";
        case Op::SEND_SUPER:         return "SEND_SUPER";
        case Op::JUMP:               return "JUMP";
        case Op::JUMP_IF_TRUE:       return "JUMP_IF_TRUE";
        case Op::JUMP_IF_FALSE:      return "JUMP_IF_FALSE";
        case Op::RETURN:             return "RETURN";
        case Op::RETURN_TOP:         return "RETURN_TOP";
        case Op::PUSH_BLOCK:         return "PUSH_BLOCK";
        case Op::DUP_RECEIVER:       return "DUP_RECEIVER";
        case Op::PUSH_CAPTURED:      return "PUSH_CAPTURED";
        case Op::STORE_CAPTURED:     return "STORE_CAPTURED";
        case Op::PUSH_GLOBAL:        return "PUSH_GLOBAL";
        case Op::STORE_GLOBAL:       return "STORE_GLOBAL";
        case Op::PUSH_INSTVAR:       return "PUSH_INSTVAR";
        case Op::STORE_INSTVAR:      return "STORE_INSTVAR";
        case Op::MAKE_CAPTURED:      return "MAKE_CAPTURED";
        case Op::MAKE_ARRAY:         return "MAKE_ARRAY";
        case Op::JUMP_BACK:          return "JUMP_BACK";
        case Op::BIN_INT_ADD:        return "BIN_INT_ADD";
        case Op::BIN_INT_SUB:        return "BIN_INT_SUB";
        case Op::BIN_INT_LE:         return "BIN_INT_LE";
        case Op::BIN_INT_LT:         return "BIN_INT_LT";
        case Op::BIN_INT_GE:         return "BIN_INT_GE";
        case Op::BIN_INT_GT:         return "BIN_INT_GT";
        case Op::BIN_INT_EQ:         return "BIN_INT_EQ";
        case Op::ASSERT_BOOL_OR_DNU: return "ASSERT_BOOL_OR_DNU";
        case Op::SEND_CALL:          return "SEND_CALL";
        case Op::DEFINE_CAPTURED:    return "DEFINE_CAPTURED";
        case Op::STORE_CLASSVAR:     return "STORE_CLASSVAR";
        case Op::PUSH_OWN_INSTVAR:   return "PUSH_OWN_INSTVAR";
        case Op::IVAR_GROUP:         return "IVAR_GROUP";
        case Op::IVAR_GROUP_END:     return "IVAR_GROUP_END";
        case Op::EXTEND:             return "EXTEND";
        case Op::HALT:               return "HALT";
    }
    return nullptr;
}

// A string literal as Smalltalk writes it, with control characters escaped
// so a listing stays one line per constant.
std::string quoted(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')      out += "''";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out + "'";
}

std::string constText(const BytecodeModule& m, size_t i) {
    using K = BytecodeModule::ConstKind;
    char buf[64];
    switch (m.constKind(i)) {
        case K::Integer:
            return std::to_string(m.constInteger(i));
        case K::Float:
            std::snprintf(buf, sizeof buf, "%.17g", m.constFloat(i));
            return buf;
        case K::LargeInteger:
            return m.constString(i) + " (radix " + std::to_string(m.constInteger(i)) + ")";
        case K::String:      return quoted(m.constString(i));
        case K::Symbol:      return "#" + m.constSymbol(i);
        case K::Char:        return "$" + m.constString(i);
        case K::BlockRef:    return "block " + std::to_string(m.constBlockRef(i));
        case K::NilK:        return "nil";
        case K::TrueK:       return "true";
        case K::FalseK:      return "false";
        case K::UnsetMarker: return "<unset>";
    }
    return "?";
}

const char* constKindName(BytecodeModule::ConstKind k) {
    using K = BytecodeModule::ConstKind;
    switch (k) {
        case K::Integer:      return "integer";
        case K::Float:        return "float";
        case K::LargeInteger: return "largeint";
        case K::String:       return "string";
        case K::Symbol:       return "symbol";
        case K::Char:         return "char";
        case K::BlockRef:     return "blockref";
        case K::NilK:         return "nil";
        case K::TrueK:        return "true";
        case K::FalseK:       return "false";
        case K::UnsetMarker:  return "unset";
    }
    return "?";
}

// The name of a constant used as a selector or variable name, or a note
// when the operand does not index a Symbol constant.
std::string nameAt(const BytecodeModule& m, unsigned int i) {
    if (i >= m.numConsts()) return "<constant out of range>";
    if (m.constKind(i) != BytecodeModule::ConstKind::Symbol)
        return "<not a symbol: " + constText(m, i) + ">";
    return m.constSymbol(i);
}

std::string slotText(const BytecodeModule& m, unsigned int slot) {
    const std::string& n = m.localName(slot);
    return n.empty() ? "slot " + std::to_string(slot) : n;
}

int keywordArity(const std::string& selector) {
    int n = 0;
    for (char c : selector) if (c == ':') ++n;
    return n;
}

// One decoded instruction: the word it starts at, its word count (EXTEND
// prefixes included), the opcode and the combined operand.
struct Instr {
    size_t word;
    size_t words;
    Op op;
    unsigned int arg;
};

std::vector<Instr> decode(const BytecodeModule& m) {
    std::vector<Instr> out;
    const auto& bytes = m.bytes();
    size_t pc = 0;
    while (pc + 1 < bytes.size()) {
        Instr in{pc / kInstrSize, 1, static_cast<Op>(bytes[pc]), bytes[pc + 1]};
        pc += kInstrSize;
        while (in.op == Op::EXTEND && pc + 1 < bytes.size()) {
            in.op = static_cast<Op>(bytes[pc]);
            in.arg = (in.arg << 8) | bytes[pc + 1];
            pc += kInstrSize;
            ++in.words;
        }
        out.push_back(in);
    }
    return out;
}

void listModule(const BytecodeModule& m, const std::string& path, std::string& out) {
    char buf[256];
    out += "module " + path + ": "
         + (m.debugName().empty() ? std::string("<unnamed>") : m.debugName()) + "\n";

    // Frame layout. The engine floors the local count at 4 (kMinLocals in
    // ExecutionEngine.cpp) and gives the operand stack 48 slots plus the
    // module's extra need (cachedExtraOperandStack).
    const int argc = m.argCount();
    const unsigned int locals = m.cachedLocalCount(static_cast<unsigned int>(argc));
    out += "  args " + std::to_string(argc) + ", locals " + std::to_string(locals);
    if (!m.localNames().empty()) {
        out += " (";
        for (size_t i = 0; i < m.localNames().size(); ++i) {
            if (i) out += ", ";
            out += std::to_string(i) + " " + slotText(m, static_cast<unsigned int>(i));
        }
        out += ")";
    }
    out += ", stack 48";
    if (unsigned int extra = m.cachedExtraOperandStack()) out += " + " + std::to_string(extra);
    out += "\n";
    if (!m.definingClass().empty())
        out += "  defining class " + m.definingClass() + "\n";
    if (m.isCallForm()) {
        out += "  call form: " + std::to_string(m.callPosArity()) + " positional";
        for (const auto& k : m.callNamedKeys()) out += ", " + k + "=";
        out += "\n";
    }
    for (const auto& s : m.scopedLocals()) {
        std::snprintf(buf, sizeof buf, "  slot %zu is '%s' in words %zu..%zu\n",
                      s.slot, s.name.c_str(), s.fromPc / kInstrSize, s.toPc / kInstrSize);
        out += buf;
    }
    if (m.numInstVarGroupEntries()) {
        out += "  write groups:\n";
        for (size_t i = 0; i < m.numInstVarGroupEntries(); ++i) {
            const auto& e = m.instVarGroupEntry(i);
            std::snprintf(buf, sizeof buf, "    @%-3zu %s, write %u of %u\n", i,
                          nameAt(m, e.nameConst).c_str(),
                          static_cast<unsigned>(e.position) + 1,
                          static_cast<unsigned>(e.count));
            out += buf;
        }
    }

    if (m.numConsts()) {
        out += "  constants:\n";
        for (size_t i = 0; i < m.numConsts(); ++i) {
            std::snprintf(buf, sizeof buf, "    #%-3zu %-9s ", i, constKindName(m.constKind(i)));
            out += buf;
            out += constText(m, i) + "\n";
        }
    }

    out += "  code:\n";
    for (const Instr& in : decode(m)) {
        const char* name = opName(in.op);
        std::string op = name ? name : "unknown opcode " + std::to_string(static_cast<unsigned>(in.op));
        const size_t next = in.word + in.words;
        std::string operand, note;
        auto withOperand = [&](std::string comment) {
            operand = std::to_string(in.arg);
            note = std::move(comment);
        };
        switch (in.op) {
            case Op::PUSH_CONST:
                withOperand(in.arg < m.numConsts() ? constText(m, in.arg)
                                                   : "<constant out of range>");
                break;
            case Op::PUSH_LOCAL:
            case Op::STORE_LOCAL:
                withOperand(slotText(m, in.arg));
                break;
            case Op::SEND_UNARY:
            case Op::SEND_BINARY:
            case Op::BIN_INT_ADD: case Op::BIN_INT_SUB:
            case Op::BIN_INT_LE:  case Op::BIN_INT_LT:
            case Op::BIN_INT_GE:  case Op::BIN_INT_GT:
            case Op::BIN_INT_EQ:
            case Op::ASSERT_BOOL_OR_DNU:
                withOperand("#" + nameAt(m, in.arg));
                break;
            case Op::SEND_KEYWORD:
            case Op::SEND_SUPER: {
                const std::string sel = nameAt(m, in.arg);
                const int n = keywordArity(sel);
                withOperand("#" + sel + ", " + std::to_string(n) + (n == 1 ? " arg" : " args"));
                break;
            }
            case Op::SEND_CALL:
                withOperand("call " + nameAt(m, in.arg));
                break;
            case Op::PUSH_GLOBAL:      case Op::STORE_GLOBAL:
            case Op::PUSH_CAPTURED:    case Op::STORE_CAPTURED:
            case Op::DEFINE_CAPTURED:
            case Op::PUSH_INSTVAR:     case Op::STORE_INSTVAR:
            case Op::PUSH_OWN_INSTVAR: case Op::STORE_CLASSVAR:
                withOperand(nameAt(m, in.arg));
                break;
            case Op::JUMP:
            case Op::JUMP_IF_TRUE:
            case Op::JUMP_IF_FALSE:
                withOperand("-> " + std::to_string(next + in.arg));
                break;
            case Op::JUMP_BACK:
                withOperand(in.arg <= next ? "-> " + std::to_string(next - in.arg)
                                           : std::string("<before the start>"));
                break;
            case Op::PUSH_BLOCK:
                if (in.arg < m.numBlocks()) {
                    const std::string& dn = m.block(in.arg).debugName();
                    withOperand(path + "." + std::to_string(in.arg)
                                + (dn.empty() ? std::string() : " (" + dn + ")"));
                } else {
                    withOperand("<block out of range>");
                }
                break;
            case Op::MAKE_ARRAY:
                withOperand(std::to_string(in.arg) + (in.arg == 1 ? " element" : " elements"));
                break;
            case Op::MAKE_CAPTURED:
                withOperand(in.arg == 1 ? "child of the inherited dict" : "method root dict");
                break;
            case Op::DUP_RECEIVER:
                withOperand("depth " + std::to_string(in.arg));
                break;
            case Op::IVAR_GROUP:
            case Op::IVAR_GROUP_END:
                if (in.arg < m.numInstVarGroupEntries()) {
                    const auto& e = m.instVarGroupEntry(in.arg);
                    withOperand(nameAt(m, e.nameConst) + ", write "
                                + std::to_string(e.position + 1) + " of "
                                + std::to_string(e.count));
                } else {
                    withOperand("<group entry out of range>");
                }
                break;
            default:
                // No operand (the compiler emits 0); show one only if set.
                if (in.arg != 0 || !name) operand = std::to_string(in.arg);
                break;
        }
        if (in.words > 1) note += note.empty() ? "(wide)" : " (wide)";
        std::snprintf(buf, sizeof buf, "    %3zu  %-18s %-6s", in.word, op.c_str(), operand.c_str());
        std::string line = buf;
        if (!note.empty()) line += "; " + note;
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line + "\n";
    }

    for (size_t i = 0; i < m.numBlocks(); ++i) {
        out += "\n";
        listModule(m.block(i), path + "." + std::to_string(i), out);
    }
}

} // namespace

std::string disassemble(const BytecodeModule& m, const std::string& path) {
    std::string out;
    listModule(m, path, out);
    return out;
}

} // namespace protoST
