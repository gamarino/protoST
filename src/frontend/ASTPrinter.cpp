#include "ASTPrinter.h"
#include <sstream>

namespace protoST {
using namespace ast;

namespace {
void emit(std::ostringstream& os, const Node& n, int depth);

void indent(std::ostringstream& os, int depth) {
    for (int i = 0; i < depth; ++i) os << "  ";
}

void emitChildren(std::ostringstream& os, const Node& n, int depth) {
    for (auto& ch : n.children) {
        os << "\n";
        indent(os, depth + 1);
        emit(os, *ch, depth + 1);
    }
}

void emit(std::ostringstream& os, const Node& n, int depth) {
    switch (n.kind) {
        case NodeKind::Module:        os << "(module"; emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::IntegerLit:    os << "(int "    << n.intValue << ")"; return;
        case NodeKind::FloatLit:      os << "(float "  << n.floatValue << ")"; return;
        case NodeKind::StringLit:     os << "(str '"   << n.text << "')"; return;
        case NodeKind::SymbolLit:     os << "(sym "    << n.text << ")"; return;
        case NodeKind::CharLit:       os << "(char $"  << n.text << ")"; return;
        case NodeKind::TrueLit:       os << "(true)";  return;
        case NodeKind::FalseLit:      os << "(false)"; return;
        case NodeKind::NilLit:        os << "(nil)";   return;
        case NodeKind::Self:          os << "(self)";  return;
        case NodeKind::Super:         os << "(super)"; return;
        case NodeKind::ThisContext:   os << "(this-context)"; return;
        case NodeKind::Identifier:    os << "(id " << n.text << ")"; return;
        case NodeKind::ArrayLit:      os << "(array";       emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::DynArrayLit:   os << "(dyn-array";   emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::Assignment:    os << "(assign " << n.text;
                                       if (!n.children.empty()) { os << " "; emit(os, *n.children[0], depth); }
                                       os << ")"; return;
        case NodeKind::UnarySend:     os << "(unary "  << n.text;
                                       for (auto& c : n.children) { os << " "; emit(os, *c, depth); }
                                       os << ")"; return;
        case NodeKind::BinarySend:    os << "(binary " << n.text;
                                       for (auto& c : n.children) { os << " "; emit(os, *c, depth); }
                                       os << ")"; return;
        case NodeKind::KeywordSend:   os << "(keyword " << n.text;
                                       for (auto& c : n.children) { os << " "; emit(os, *c, depth); }
                                       os << ")"; return;
        case NodeKind::Cascade:       os << "(cascade"; emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::Block: {
            os << "(block argc=" << n.intValue << " names=(";
            for (size_t i = 0; i < n.stringList.size(); ++i) { if (i) os << " "; os << n.stringList[i]; }
            os << ")";
            emitChildren(os, n, depth);
            os << ")"; return;
        }
        case NodeKind::Return:        os << "(^ ";
                                       if (!n.children.empty()) emit(os, *n.children[0], depth);
                                       os << ")"; return;
        case NodeKind::MethodDecl: {
            os << "(method-decl " << n.text << " " << n.stringList[0] << " " << n.intValue << " (";
            for (int i = 0; i < n.intValue; ++i) {
                if (i) os << " ";
                os << n.stringList[1 + i];
            }
            os << ")";
            if (n.boolFlag) os << " class-side";
            emitChildren(os, n, depth);
            os << ")"; return;
        }
        case NodeKind::ClassDecl: {
            os << "(class-decl " << n.text << " " << n.stringList[0] << " (inst-vars";
            for (size_t i = 1; i < n.stringList.size(); ++i) os << " " << n.stringList[i];
            os << "))"; return;
        }
    }
}
} // anon

std::string astToString(const Node& n) {
    std::ostringstream os;
    emit(os, n, 0);
    os << "\n";
    return os.str();
}

} // namespace protoST
