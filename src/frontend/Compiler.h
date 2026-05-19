#pragma once
#include "AST.h"
#include "../runtime/BytecodeModule.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoST {

class Compiler {
public:
    std::unique_ptr<BytecodeModule> compileModule(const ast::Node& mod);
    bool hasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    struct Scope {
        std::unordered_map<std::string, int> slots; // name -> slot index
        int nextSlot = 0;
    };

    std::vector<Scope> scopes_;
    std::vector<std::string> errors_;

    void   emitExpr(BytecodeModule& m, const ast::Node& n);
    void   emitStatement(BytecodeModule& m, const ast::Node& n);
    int    declareLocal(const std::string& name);
    int    resolveLocal(const std::string& name) const;
    void   error(const std::string& msg);
};

} // namespace protoST
