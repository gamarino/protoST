#pragma once
#include "AST.h"
#include "../runtime/BytecodeModule.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace protoST {

class Compiler {
public:
    std::unique_ptr<BytecodeModule> compileModule(const ast::Node& mod);
    bool hasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& errors() const { return errors_; }

    // Per-scope analysis result. For F3-C1: just the captured names.
    // Computed by analyseClosures(mod) before emission.
    struct ScopeAnalysis {
        // For each Block/MethodDecl AST node pointer, the names this scope DECLARES
        // that any inner block REFERENCES (i.e., must be stored in captured dict).
        std::unordered_map<const ast::Node*, std::unordered_set<std::string>> capturedByScope;
        // Top-level captured names (module scope) — keyed by nullptr.
        std::unordered_set<std::string> moduleCaptured;
    };

    void analyseClosures(const ast::Node& module);
    const ScopeAnalysis& analysis() const { return analysis_; }

private:
    struct Scope {
        std::unordered_map<std::string, int> slots; // name -> slot index
        int nextSlot = 0;
    };

    std::vector<Scope> scopes_;
    std::vector<std::string> errors_;
    ScopeAnalysis analysis_;

    void   emitExpr(BytecodeModule& m, const ast::Node& n);
    void   emitStatement(BytecodeModule& m, const ast::Node& n);
    int    declareLocal(const std::string& name);
    int    resolveLocal(const std::string& name) const;
    void   error(const std::string& msg);
};

} // namespace protoST
