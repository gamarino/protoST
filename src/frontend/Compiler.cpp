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
    // True if this scope is a Block. In Smalltalk a block does NOT introduce
    // new bindings through assignment — only through its argument list and
    // its `| ... |` temps. So inside a block, the LHS of `:=` is a *reference*
    // to a name owned by some enclosing scope, not a declaration. At module
    // and method scope, by contrast, first-seen assignment is the declaration
    // site (preserves pre-F3-C5 semantics for top-level temps).
    bool isBlock = false;
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
            // The LHS is treated differently depending on scope kind:
            //   * Module / method scope: assignment IS the declaration site
            //     (Smalltalk doesn't require a `|...|` pipe at module level).
            //   * Block scope: an assignment is *not* a declaration — it
            //     refers to a name owned by some enclosing scope. We record
            //     it as a direct reference so it bubbles up as a free var
            //     and lands in an outer scope's captured dict (F3-C5).
            if (!n.text.empty()) {
                if (cur.isBlock) cur.directRefs.insert(n.text);
                else             cur.declared.insert(n.text);
            }
            if (!n.children.empty()) walkNode(*n.children[0], cur, out, nullptr);
            return;
        }

        case NodeKind::Block: {
            // Open a fresh scope for the block.
            ScopeWalker blockScope;
            blockScope.isBlock = true;
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
    // F7-REPL: in REPL mode a top-level assignment is not a declaration of a
    // module local — it binds a persistent global. Marking the module scope
    // as a "block" makes the analysis treat `x := ...` as a reference, so the
    // name is never added to `declared` and therefore never enters the
    // module captured dict. Inner blocks that reference it then fall through
    // to PUSH_GLOBAL, consistent with the STORE_GLOBAL emitted below.
    moduleScope.isBlock = replMode_;
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

void Compiler::collectClasses(const Node& module) {
    classes_.clear();
    if (module.kind != NodeKind::Module) return;
    for (const auto& topPtr : module.children) {
        if (!topPtr || topPtr->kind != NodeKind::ClassDecl) continue;
        const auto& cd = *topPtr;
        ClassInfo info;
        info.name = cd.text;
        // ClassDecl AST shape (see Parser::parseClassDecl):
        //   stringList[0] = superclass name; stringList[1..] = inst var names.
        if (!cd.stringList.empty()) info.superclassName = cd.stringList[0];
        for (size_t i = 1; i < cd.stringList.size(); ++i) {
            info.instVarNames.push_back(cd.stringList[i]);
        }
        classes_[info.name] = std::move(info);
    }
}

std::unique_ptr<BytecodeModule> Compiler::compileModule(const Node& mod) {
    // F4-U2: gather per-class info before emission so MethodDecl emission
    // (F4-U3+) can resolve instance-variable names to slot indices.
    collectClasses(mod);
    // Run closure analysis up-front so that emission can decide between
    // slot-based and captured-dict-based variable access.
    analyseClosures(mod);

    auto bc = std::make_unique<BytecodeModule>();
    scopes_.clear();
    scopes_.emplace_back();
    {
        auto& s = scopes_.back();
        s.astNode = nullptr; // module scope
        s.capturedNames = analysis_.moduleCaptured;
    }
    if (mod.children.empty()) {
        bc->emit(Op::PUSH_NIL, 0, currentLine_);
    } else {
        for (size_t i = 0; i < mod.children.size(); ++i) {
            emitStatement(*bc, *mod.children[i]);
            // For all but the last statement, discard the result.
            if (i + 1 != mod.children.size()) bc->emit(Op::POP, 0, currentLine_);
        }
    }
    bc->emit(Op::RETURN_TOP, 0, currentLine_);
    recordLocalNames(*bc);
    bc->setDebugName("<module>");
    return bc;
}

void Compiler::emitStatement(BytecodeModule& m, const Node& n) {
    // F8-1: track the current source line for instruction line-mapping.
    if (n.line > 0) currentLine_ = n.line;
    if (n.kind == NodeKind::Return) {
        if (!n.children.empty()) emitExpr(m, *n.children[0]);
        else                     m.emit(Op::PUSH_NIL, 0, currentLine_);
        m.emit(Op::RETURN, 0, currentLine_);
        return;
    }
    if (n.kind == NodeKind::ClassDecl) {
        // F4-U2: at runtime, materialize the new class by sending #newChild
        // to the superclass global, then bind it under the class name in
        // globals. We DUP before STORE_GLOBAL so the value remains on the
        // stack as this statement's result (matching the Assignment pattern;
        // the module-level loop will POP it between statements).
        const std::string& superName =
            n.stringList.empty() ? std::string("Object") : n.stringList[0];
        auto superIdx     = m.internSymbol(superName);
        auto newChildIdx  = m.internSymbol("newChild");
        auto classNameIdx = m.internSymbol(n.text);
        // BL-2: indices may exceed 255 — emitWide prefixes EXTEND words as
        // needed, so there is no longer a 256-symbol ceiling.
        m.emitWide(Op::PUSH_GLOBAL,  static_cast<unsigned int>(superIdx), currentLine_);
        m.emitWide(Op::SEND_UNARY,   static_cast<unsigned int>(newChildIdx), currentLine_);
        // BL-3: stamp the declared class name onto the fresh class object so
        // printString can render instances as "a Counter". We send
        // `__setClassName:` (a keyword primitive on objectProto) with the name
        // as a string literal; the send leaves the class object on the stack
        // (the primitive returns its receiver). DUP then keeps a copy as the
        // statement's value while STORE_GLOBAL binds the class under its name.
        {
            auto setNameIdx = m.internSymbol("__setClassName:");
            auto nameStrIdx = m.addString(n.text);
            m.emitWide(Op::PUSH_CONST,   static_cast<unsigned int>(nameStrIdx), currentLine_);
            m.emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(setNameIdx), currentLine_);
        }
        m.emit(Op::DUP,          0, currentLine_);
        m.emitWide(Op::STORE_GLOBAL, static_cast<unsigned int>(classNameIdx), currentLine_);
        return;
    }
    if (n.kind == NodeKind::MethodDecl) {
        // F4-U3: compile the method body as a sub-BytecodeModule and emit
        // module-level bytecode that, at runtime, installs the resulting
        // method wrapper on the target class.
        //
        // AST shape (see Parser::parseMethodDecl):
        //   n.text                            = class name
        //   n.stringList[0]                   = selector
        //   n.stringList[1..n.intValue]       = arg names
        //   n.stringList[n.intValue+1..]      = local names
        //   n.children                        = body statements
        //   n.boolFlag                        = true if class-side method
        //
        // F4-U3 v1 ignores n.boolFlag (class-side methods get installed on
        // the class proto, same as instance-side). F4-v2 will split via a
        // proper classside parent.

        // F4-U5: record the current method's class + its instance-variable
        // names BEFORE emitting the body, so that emitExpr/emitStatement
        // can resolve identifiers against this class's inst vars before
        // falling back to globals.
        currentMethodClass_ = n.text;
        {
            auto it = classes_.find(n.text);
            if (it != classes_.end()) currentInstVars_ = it->second.instVarNames;
            else                      currentInstVars_.clear();
        }

        // Build sub-BytecodeModule for the method body.
        auto sub = std::make_unique<BytecodeModule>();

        // Open fresh scope; slot 0 = "self", slots 1..nArgs = args,
        // remaining slots = method locals.
        scopes_.emplace_back();
        {
            auto& s = scopes_.back();
            // CLO Part 2: a method IS a closure scope boundary. Load the set
            // of names that inner blocks capture from this method so that
            // isCaptured() routes those names to PUSH_CAPTURED/STORE_CAPTURED.
            s.astNode = &n;
            auto it = analysis_.capturedByScope.find(&n);
            if (it != analysis_.capturedByScope.end()) {
                s.capturedNames = it->second;
            }
        }
        int nArgs = static_cast<int>(n.intValue);
        declareLocal("self");                              // slot 0
        for (int i = 0; i < nArgs; ++i) {
            // CLO Part 2: a captured argument keeps a real local slot too —
            // pushFrame binds the incoming value there and the prologue
            // copies it into the captured dict. Declaring it unconditionally
            // keeps slot numbering stable for the copy-in PUSH_LOCAL.
            declareLocal(n.stringList[1 + i]);             // slots 1..nArgs
        }
        for (size_t i = static_cast<size_t>(1 + nArgs); i < n.stringList.size(); ++i) {
            declareLocal(n.stringList[i]);                 // method locals
        }
        sub->setArgCount(nArgs + 1);  // +1 for self

        // CLO Part 2: method prologue. If any name is captured by an inner
        // block, allocate the method's captured dict (MAKE_CAPTURED) and copy
        // each captured ARGUMENT's incoming value from its local slot into
        // the dict. Captured method locals/temps need no copy — the body
        // assigns them via STORE_CAPTURED.
        emitCaptureProlog(*sub, /*isMethod=*/true, n.stringList, /*nArgs=*/nArgs,
                          /*argNameOffset=*/1);

        // Emit body statements.
        if (n.children.empty()) {
            sub->emit(Op::PUSH_NIL, 0, currentLine_);
        }
        for (size_t i = 0; i < n.children.size(); ++i) {
            emitStatement(*sub, *n.children[i]);
            if (i + 1 != n.children.size()) sub->emit(Op::POP, 0, currentLine_);
        }
        // ST-80 default return: a method returns `self` if there is no
        // explicit `^`. If the last statement is already a Return, the
        // body's RETURN was already emitted by emitStatement.
        bool endsWithReturn = !n.children.empty()
            && n.children.back()->kind == NodeKind::Return;
        if (!endsWithReturn) {
            // Discard the last expression's value, push self, and return.
            if (!n.children.empty()) sub->emit(Op::POP, 0, currentLine_);
            sub->emit(Op::PUSH_LOCAL, 0, currentLine_);  // self
            sub->emit(Op::RETURN, 0, currentLine_);
        }

        recordLocalNames(*sub);
        sub->setDebugName(n.text + ">>" +
                          (n.stringList.empty() ? std::string("<method>")
                                                : n.stringList[0]));
        // BL-1: record the defining class so the engine can resolve `super`
        // sends inside this method body (lookup starts at the class's parent).
        sub->setDefiningClass(n.text);
        scopes_.pop_back();

        // Attach the method module as a sub-block of the outer (module) bytecode.
        size_t blkIdx = m.addBlockModule(std::move(sub));

        // Module-level emission: load class, push method wrapper, push selector
        // symbol, send #__installMethod:as: to the class. The send returns the
        // class (the receiver), leaving it on the stack. The surrounding module
        // loop's POP separator (or RETURN_TOP) consumes it cleanly.
        auto classIdx    = m.internSymbol(n.text);
        auto selectorIdx = m.internSymbol(n.stringList[0]);
        auto installIdx  = m.internSymbol("__installMethod:as:");

        m.emitWide(Op::PUSH_GLOBAL,  static_cast<unsigned int>(classIdx), currentLine_);    // class
        m.emitWide(Op::PUSH_BLOCK,   static_cast<unsigned int>(blkIdx), currentLine_);      // method wrapper
        m.emitWide(Op::PUSH_CONST,   static_cast<unsigned int>(selectorIdx), currentLine_); // selector symbol
        m.emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(installIdx), currentLine_);  // → class

        // F4-U5: clear the method-body name-resolution context.
        currentMethodClass_.clear();
        currentInstVars_.clear();
        return;
    }
    if (n.kind == NodeKind::Assignment) {
        // Statement-level assignment. If the name is captured-by-an-inner-block,
        // it lives in the shared captured dict; otherwise it gets a local slot.
        // In both cases we DUP after evaluating the RHS so the assigned value
        // remains on the stack for the top-level POP separator (or RETURN_TOP,
        // when it is the last statement).
        if (isCaptured(n.text)) {
            emitExpr(m, *n.children[0]);
            auto sym = m.internSymbol(n.text);
            m.emit(Op::DUP, 0, currentLine_);
            m.emitWide(Op::STORE_CAPTURED, static_cast<unsigned int>(sym), currentLine_);
            return;
        }
        // F4-U5: instance variable of the current method's class.
        for (const auto& iv : currentInstVars_) {
            if (iv == n.text) {
                emitExpr(m, *n.children[0]);
                auto sym = m.internSymbol(n.text);
                m.emit(Op::DUP, 0, currentLine_);
                m.emitWide(Op::STORE_INSTVAR, static_cast<unsigned int>(sym), currentLine_);
                return;
            }
        }
        // F7-REPL: at module scope, a top-level assignment binds a persistent
        // global so it is visible to subsequently-compiled REPL inputs.
        if (replMode_ && atModuleScope()) {
            emitExpr(m, *n.children[0]);
            auto sym = m.internSymbol(n.text);
            m.emit(Op::DUP, 0, currentLine_);
            m.emitWide(Op::STORE_GLOBAL, static_cast<unsigned int>(sym), currentLine_);
            return;
        }
        int slot = declareLocal(n.text);
        emitExpr(m, *n.children[0]);
        m.emit(Op::DUP, 0, currentLine_);
        m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slot), currentLine_);
        return;
    }
    emitExpr(m, n);
}

void Compiler::emitExpr(BytecodeModule& m, const Node& n) {
    // F8-1: track the current source line for instruction line-mapping.
    if (n.line > 0) currentLine_ = n.line;
    switch (n.kind) {
        case NodeKind::IntegerLit: {
            auto idx = m.addInteger(n.intValue);
            // BL-2: constant-pool indices may exceed 255 — emitWide prefixes
            // EXTEND words as needed, lifting the old 256-constant ceiling.
            m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(idx), currentLine_);
            return;
        }
        case NodeKind::FloatLit: {
            auto idx = m.addFloat(n.floatValue);
            m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(idx), currentLine_);
            return;
        }
        case NodeKind::StringLit: {
            auto idx = m.addString(n.text);
            m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(idx), currentLine_);
            return;
        }
        case NodeKind::SymbolLit: {
            auto idx = m.internSymbol(n.text);
            m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(idx), currentLine_);
            return;
        }
        case NodeKind::CharLit: {
            auto idx = m.addChar(n.text);
            m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(idx), currentLine_);
            return;
        }
        case NodeKind::TrueLit:  m.emit(Op::PUSH_TRUE, 0, currentLine_); return;
        case NodeKind::FalseLit: m.emit(Op::PUSH_FALSE, 0, currentLine_); return;
        case NodeKind::NilLit:   m.emit(Op::PUSH_NIL, 0, currentLine_); return;
        case NodeKind::Self:     m.emit(Op::PUSH_SELF, 0, currentLine_); return;
        case NodeKind::Super:    m.emit(Op::PUSH_SUPER, 0, currentLine_); return;
        case NodeKind::Identifier: {
            // Captured-dict path: any ancestor scope marked this name as
            // captured by an inner block.
            if (isCaptured(n.text)) {
                auto sym = m.internSymbol(n.text);
                m.emitWide(Op::PUSH_CAPTURED, static_cast<unsigned int>(sym), currentLine_);
                return;
            }
            int slot = resolveLocal(n.text);
            if (slot >= 0) {
                m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slot), currentLine_);
                return;
            }
            // F4-U5: instance variable of the current method's class.
            // Only consulted while emitting a method body (currentInstVars_
            // is populated by the MethodDecl branch in emitStatement).
            for (const auto& iv : currentInstVars_) {
                if (iv == n.text) {
                    auto sym = m.internSymbol(n.text);
                    m.emitWide(Op::PUSH_INSTVAR, static_cast<unsigned int>(sym), currentLine_);
                    return;
                }
            }
            // F4-U3: fall back to the global namespace.
            // ST-80 semantics: free identifiers in expressions resolve through
            // the scope chain, then globals. A failed runtime lookup will
            // throw "undefined global: X" with the actual name.
            auto symIdx = m.internSymbol(n.text);
            m.emitWide(Op::PUSH_GLOBAL, static_cast<unsigned int>(symIdx), currentLine_);
            return;
        }
        case NodeKind::Assignment: {
            // Expression-position assignment. If the name is captured, write
            // into the shared captured dict; otherwise use a local slot.
            // In both cases we DUP so the value is left on the stack as the
            // expression's result.
            if (isCaptured(n.text)) {
                emitExpr(m, *n.children[0]);
                auto sym = m.internSymbol(n.text);
                m.emit(Op::DUP, 0, currentLine_);
                m.emitWide(Op::STORE_CAPTURED, static_cast<unsigned int>(sym), currentLine_);
                return;
            }
            // F4-U5: instance variable of the current method's class.
            for (const auto& iv : currentInstVars_) {
                if (iv == n.text) {
                    emitExpr(m, *n.children[0]);
                    auto sym = m.internSymbol(n.text);
                    m.emit(Op::DUP, 0, currentLine_);
                    m.emitWide(Op::STORE_INSTVAR, static_cast<unsigned int>(sym), currentLine_);
                    return;
                }
            }
            // F7-REPL: module-scope assignment-expression binds a global.
            if (replMode_ && atModuleScope()) {
                emitExpr(m, *n.children[0]);
                auto sym = m.internSymbol(n.text);
                m.emit(Op::DUP, 0, currentLine_);
                m.emitWide(Op::STORE_GLOBAL, static_cast<unsigned int>(sym), currentLine_);
                return;
            }
            int slot = declareLocal(n.text);
            emitExpr(m, *n.children[0]);
            m.emit(Op::DUP, 0, currentLine_);
            m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slot), currentLine_);
            return;
        }
        case NodeKind::UnarySend: {
            emitExpr(m, *n.children[0]);
            auto sym = m.internSymbol(n.text);
            m.emitWide(Op::SEND_UNARY, static_cast<unsigned int>(sym), currentLine_);
            return;
        }
        case NodeKind::BinarySend: {
            // receiver, then arg, then SEND_BINARY
            emitExpr(m, *n.children[0]);
            if (n.children.size() < 2) { error("binary send missing argument"); return; }
            emitExpr(m, *n.children[1]);
            auto sym = m.internSymbol(n.text);
            m.emitWide(Op::SEND_BINARY, static_cast<unsigned int>(sym), currentLine_);
            return;
        }
        case NodeKind::KeywordSend: {
            // receiver, then each arg in order, then SEND_KEYWORD
            emitExpr(m, *n.children[0]);
            for (size_t i = 1; i < n.children.size(); ++i) {
                emitExpr(m, *n.children[i]);
            }
            auto sym = m.internSymbol(n.text);
            m.emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(sym), currentLine_);
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
                m.emit(Op::DUP, 0, currentLine_); // duplicate receiver for this send
                // emit args; receiver is already on stack just below the args
                if (msg.kind == NodeKind::UnarySend) {
                    auto sym = m.internSymbol(msg.text);
                    m.emitWide(Op::SEND_UNARY, static_cast<unsigned int>(sym), currentLine_);
                } else if (msg.kind == NodeKind::BinarySend) {
                    if (msg.children.empty()) { error("cascade binary missing arg"); }
                    else emitExpr(m, *msg.children[0]);
                    auto sym = m.internSymbol(msg.text);
                    m.emitWide(Op::SEND_BINARY, static_cast<unsigned int>(sym), currentLine_);
                } else if (msg.kind == NodeKind::KeywordSend) {
                    for (auto& a : msg.children) emitExpr(m, *a);
                    auto sym = m.internSymbol(msg.text);
                    m.emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(sym), currentLine_);
                } else {
                    error("unexpected node in cascade");
                }
                if (isLast) {
                    // stack: [receiver, last_result]; stash result, pop receiver, push result back
                    m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(tmp), currentLine_);
                    m.emit(Op::POP, 0, currentLine_);            // discard receiver
                    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(tmp), currentLine_);
                } else {
                    m.emit(Op::POP, 0, currentLine_);            // discard this result, keep receiver
                }
            }
            return;
        }
        case NodeKind::Block: {
            auto sub = std::make_unique<BytecodeModule>();
            // open fresh scope for block
            scopes_.emplace_back();
            {
                auto& s = scopes_.back();
                s.astNode = &n;
                auto it = analysis_.capturedByScope.find(&n);
                if (it != analysis_.capturedByScope.end()) {
                    s.capturedNames = it->second;
                }
            }
            int nArgs = static_cast<int>(n.intValue);
            sub->setArgCount(nArgs);
            // declare args first (slots 0..nArgs-1), then locals
            for (size_t i = 0; i < n.stringList.size(); ++i) {
                declareLocal(n.stringList[i]);
            }
            // CLO Part 2: a block reuses the captured dict it inherited via
            // PUSH_BLOCK's __captured__ stamp — it emits NO MAKE_CAPTURED.
            // But if one of this block's OWN arguments is captured by an
            // inner block, copy that argument's incoming value into the
            // (shared) captured dict, exactly like a method's argument
            // copy-in. The block's stringList is all args+locals; the first
            // nArgs entries are the arguments.
            emitCaptureProlog(*sub, /*isMethod=*/false, n.stringList,
                              /*nArgs=*/nArgs, /*argNameOffset=*/0);
            // emit body statements; last value implicitly returned
            if (n.children.empty()) sub->emit(Op::PUSH_NIL, 0, currentLine_);
            for (size_t i = 0; i < n.children.size(); ++i) {
                emitStatement(*sub, *n.children[i]);
                if (i + 1 != n.children.size()) sub->emit(Op::POP, 0, currentLine_);
            }
            sub->emit(Op::RETURN_TOP, 0, currentLine_);
            recordLocalNames(*sub);
            sub->setDebugName("<block>");
            scopes_.pop_back();
            size_t blkIdx = m.addBlockModule(std::move(sub));
            m.emitWide(Op::PUSH_BLOCK, static_cast<unsigned int>(blkIdx), currentLine_);
            return;
        }
        default:
            error("expression kind not yet supported");
            m.emit(Op::PUSH_NIL, 0, currentLine_);
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

bool Compiler::isCaptured(const std::string& name) const {
    // A name is captured-at-use if some enclosing scope marked it as captured
    // by an inner block. Walk innermost-first; honour shadowing: if a closer
    // scope already binds this name as a non-captured local slot, that wins
    // and the use is NOT captured.
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        if (it->capturedNames.count(name) != 0) return true;
        if (it->slots.count(name) != 0) return false;
    }
    return false;
}

void Compiler::recordLocalNames(BytecodeModule& m) const {
    // The innermost scope's `slots` map is name->slot; invert it into a
    // slot-indexed vector sized to the highest slot in use. Captured names
    // live in the closure dict rather than a local slot, but they never
    // appear in `slots`, so the inversion only covers real local slots.
    const auto& s = scopes_.back();
    std::vector<std::string> names(static_cast<size_t>(s.nextSlot));
    for (const auto& [name, slot] : s.slots) {
        if (slot >= 0 && static_cast<size_t>(slot) < names.size())
            names[static_cast<size_t>(slot)] = name;
    }
    m.setLocalNames(std::move(names));
}

void Compiler::emitCaptureProlog(BytecodeModule& m, bool isMethod,
                                 const std::vector<std::string>& argNames,
                                 int nArgs, int argNameOffset) {
    const auto& s = scopes_.back();
    if (s.capturedNames.empty()) return;
    // A method whose inner blocks capture anything needs its own captured
    // dict in frame slot 0. Blocks reuse the dict inherited via PUSH_BLOCK.
    if (isMethod) {
        m.emit(Op::MAKE_CAPTURED, 0, currentLine_);
    }
    // Copy each captured ARGUMENT's incoming value from its local slot into
    // the captured dict. Captured locals/temps need no copy — the body
    // assigns them through STORE_CAPTURED directly.
    for (int i = 0; i < nArgs; ++i) {
        const std::string& argName = argNames[static_cast<size_t>(argNameOffset + i)];
        if (s.capturedNames.count(argName) == 0) continue;
        int slot = resolveLocal(argName);
        if (slot < 0) continue;  // defensive — should always resolve
        auto sym = m.internSymbol(argName);
        m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slot), currentLine_);
        m.emitWide(Op::STORE_CAPTURED, static_cast<unsigned int>(sym), currentLine_);
    }
}

void Compiler::error(const std::string& msg) { errors_.push_back(msg); }

} // namespace protoST
