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

    // F4-U2: per-class metadata gathered by a pre-pass over the module's
    // top-level forms. Populated by collectClasses(mod) before emission.
    struct ClassInfo {
        std::string name;                       // e.g., "Counter"
        std::string superclassName;             // e.g., "Object"
        std::vector<std::string> instVarNames;  // e.g., {"value"}
    };

    void analyseClosures(const ast::Node& module);
    const ScopeAnalysis& analysis() const { return analysis_; }

    const std::unordered_map<std::string, ClassInfo>& classes() const { return classes_; }

private:
    struct Scope {
        std::unordered_map<std::string, int> slots; // name -> slot index
        int nextSlot = 0;
        // Names that should be stored in the captured dictionary instead of
        // local slots. Populated from ScopeAnalysis at scope-entry.
        std::unordered_set<std::string> capturedNames;
        // AST node pointer for this scope (nullptr for module, Block/MethodDecl
        // ast::Node* otherwise). Used to look up capturedByScope[node].
        const ast::Node* astNode = nullptr;
    };

    std::vector<Scope> scopes_;
    std::vector<std::string> errors_;
    ScopeAnalysis analysis_;
    // F4-U2: collected by collectClasses() before emission; queried by
    // downstream passes (e.g., MethodDecl emission) to map inst-var refs.
    std::unordered_map<std::string, ClassInfo> classes_;
    // F4-U5: name resolution context while emitting a method body. Set on
    // entry to MethodDecl emission, cleared after. Identifier/Assignment
    // emission consults currentInstVars_ to choose between PUSH_LOCAL,
    // PUSH_INSTVAR, and PUSH_GLOBAL.
    std::string currentMethodClass_;
    std::vector<std::string> currentInstVars_;

    void   collectClasses(const ast::Node& module);
    void   emitExpr(BytecodeModule& m, const ast::Node& n);
    void   emitStatement(BytecodeModule& m, const ast::Node& n);
    int    declareLocal(const std::string& name);
    int    resolveLocal(const std::string& name) const;
    // Returns true if `name` appears in the capturedNames set of the current
    // scope or any enclosing scope (innermost-first lookup).
    bool   isCaptured(const std::string& name) const;
    void   error(const std::string& msg);
};

} // namespace protoST
