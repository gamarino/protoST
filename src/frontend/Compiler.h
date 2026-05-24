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

    // F7-REPL: when enabled, module-scope assignments compile to STORE_GLOBAL
    // (and bare module-scope identifiers resolve through globals) instead of
    // module-local slots. This lets each REPL input — compiled as its own
    // BytecodeModule — see the bindings established by previous inputs, since
    // the runtime's `globals()` namespace persists across modules. It has no
    // effect on whole-program / script compilation, which keeps slot-based
    // module locals.
    void setReplMode(bool on) { replMode_ = on; }

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
    // F7-REPL: see setReplMode(). When true and emission is at module scope
    // (scopes_.size() == 1), top-level assignments target the global
    // namespace so REPL state persists across separately-compiled inputs.
    bool replMode_ = false;

    // F8-1: the source line currently being emitted. Updated on entry to
    // emitStatement / emitExpr from the AST node's `line` field (when valid)
    // and stamped onto every instruction via emit(). Synthesised nodes that
    // lack a line (line == 0) leave the previous value untouched.
    int currentLine_ = 0;

    // True while emitting at the outermost (module) scope.
    bool atModuleScope() const { return scopes_.size() == 1; }

    void   collectClasses(const ast::Node& module);
    void   emitExpr(BytecodeModule& m, const ast::Node& n);
    void   emitStatement(BytecodeModule& m, const ast::Node& n);
    int    declareLocal(const std::string& name);
    int    resolveLocal(const std::string& name) const;
    // F8-4: write the current (innermost) scope's slot->name mapping into `m`
    // as a slot-indexed vector, so the DAP Variables panel can show real
    // identifiers instead of "slot N". Call before popping the scope.
    void   recordLocalNames(BytecodeModule& m) const;
    // CLO Part 2: emit the closure-capture prologue for the current scope.
    // For a method (isMethod==true) with a non-empty captured set this emits
    // MAKE_CAPTURED; for both methods and blocks it then copies each captured
    // ARGUMENT's incoming value from its local slot into the captured dict
    // (PUSH_LOCAL <slot> ; STORE_CAPTURED <nameSymbol>). `argNames` is the
    // scope's full args+locals name list; the first `nArgs` entries starting
    // at `argNameOffset` are the argument names.
    void   emitCaptureProlog(BytecodeModule& m, bool isMethod,
                             const std::vector<std::string>& argNames,
                             int nArgs, int argNameOffset);
    // 2026-05-24: Compile `<receiver> doYielding: [ :elem | <body> ]` to a
    // bytecode loop using `at:` and `value:` so the block may yield mid-
    // iteration. See docs/superpowers/specs/2026-05-24-doyielding-design.md.
    void   emitDoYieldingLoop(BytecodeModule& m,
                              const ast::Node& receiverNode,
                              const ast::Node& blockNode);
    // 2026-05-24 Tier-S perf: inline canonical conditional / loop sends whose
    // arguments are literal zero-arg blocks (no locals).  Eliminates the
    // PUSH_BLOCK + SEND_KEYWORD + prim_True_ifTrue / invokeBlock chain and,
    // crucially for tight recursion (fib), the nested ExecutionEngine plus
    // the NonLocalReturn C++ throw on `^expr` from inside the block. The
    // inlined body emits into the current frame, so `^expr` becomes a plain
    // local RETURN with no stack unwind. Returns true on inline success;
    // false means the caller should fall through to normal SEND_KEYWORD.
    bool   tryEmitInlinedControl(BytecodeModule& m, const ast::Node& send);
    void   emitInlinedBlockBody(BytecodeModule& m, const ast::Node& block);
    // Patch a forward-jump instruction (emitted with placeholder arg=0) so
    // its arg targets the current emit position. Errors out if the distance
    // does not fit in the 8-bit operand (we do not yet rewrite the
    // EXTEND-prefixed form during patching). Callers should avoid inlining
    // blocks larger than ~250 instructions.
    void   patchJumpToHere(BytecodeModule& m, std::size_t jumpInstrPos);
    // Returns true if `name` appears in the capturedNames set of the current
    // scope or any enclosing scope (innermost-first lookup).
    bool   isCaptured(const std::string& name) const;
    void   error(const std::string& msg);
};

} // namespace protoST
