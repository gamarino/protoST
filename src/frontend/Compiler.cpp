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
    // D22: instance-variable names of the enclosing method's class — minus any
    // method temp/arg that shadows one. nullptr at module scope (no instance
    // variables exist there). Threaded unchanged into every nested block
    // scope. An instance variable is owned by `self`, not by any lexical
    // scope: it is accessed through PUSH_INSTVAR / STORE_INSTVAR and must
    // never enter the closure-capture machinery. Excluding it from freeVarsOf
    // keeps it out of every enclosing scope's innerNeeds, hence out of every
    // captured set — so a method that both assigns an instance variable and
    // references it from a nested block no longer boxes the variable into an
    // (uninitialised) closure dict.
    const std::unordered_set<std::string>* instVars = nullptr;
};

using ClassMap = std::unordered_map<std::string, Compiler::ClassInfo>;

// Forward declarations.
void walkNode(const Node& n, ScopeWalker& cur,
              Compiler::ScopeAnalysis& out, const Node* scopeKey,
              const ClassMap* classes);
void walkBlockBody(const Node& blockNode, ScopeWalker& blockScope,
                   Compiler::ScopeAnalysis& out, const ClassMap* classes);

// Compute the free variables a scope exposes to its parent:
//   (directRefs ∪ innerNeeds) − declared − instanceVariables.
// D22: an instance variable is excluded — it is not a free variable of any
// lexical scope, it belongs to `self`. Dropping it here stops it bubbling
// into an enclosing scope's innerNeeds and therefore into a captured set.
std::unordered_set<std::string> freeVarsOf(const ScopeWalker& sw) {
    std::unordered_set<std::string> out;
    out.reserve(sw.directRefs.size() + sw.innerNeeds.size());
    auto consider = [&](const std::string& r) {
        if (sw.declared.count(r) != 0) return;
        if (sw.instVars && sw.instVars->count(r) != 0) return;
        out.insert(r);
    };
    for (const auto& r : sw.directRefs)  consider(r);
    for (const auto& r : sw.innerNeeds)  consider(r);
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
              Compiler::ScopeAnalysis& out, const Node* /*scopeKey*/,
              const ClassMap* classes) {
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
            if (!n.children.empty()) walkNode(*n.children[0], cur, out, nullptr, classes);
            return;
        }

        case NodeKind::Block: {
            // Open a fresh scope for the block.
            ScopeWalker blockScope;
            blockScope.isBlock = true;
            // D22: a block sees the enclosing method's instance variables —
            // thread the set through unchanged so an ivar referenced inside
            // the block is excluded from the block's free vars.
            blockScope.instVars = cur.instVars;
            // n.stringList holds: nArgs args followed by locals.
            for (const auto& name : n.stringList) {
                blockScope.declared.insert(name);
            }
            walkBlockBody(n, blockScope, out, classes);
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
            // D22: the instance variables visible in this method body — the
            // declaring class's instance variables, minus any method temp/arg
            // that shadows one (within the method that name is a local). The
            // set is local to this case but is walked entirely before it goes
            // out of scope, so every nested block's `instVars` pointer into it
            // stays valid for the whole MethodDecl subtree.
            std::unordered_set<std::string> ivarSet;
            if (classes) {
                auto cit = classes->find(n.text);
                if (cit != classes->end()) {
                    for (const auto& iv : cit->second.instVarNames) {
                        ivarSet.insert(iv);
                    }
                }
            }
            for (size_t i = 1; i < n.stringList.size(); ++i) {
                ivarSet.erase(n.stringList[i]);  // a method-local shadows it
            }
            methodScope.instVars = &ivarSet;
            for (const auto& child : n.children) {
                walkNode(*child, methodScope, out, &n, classes);
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
                if (child) walkNode(*child, cur, out, nullptr, classes);
            }
            return;
    }
}

void walkBlockBody(const Node& blockNode, ScopeWalker& blockScope,
                   Compiler::ScopeAnalysis& out, const ClassMap* classes) {
    for (const auto& child : blockNode.children) {
        if (child) walkNode(*child, blockScope, out, &blockNode, classes);
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
    // Module-level: walk every statement under the module node. `classes_` is
    // populated by collectClasses() (run first in compileModule), so the
    // MethodDecl walk can resolve each method's class to its instance
    // variables — see D22 / ScopeWalker::instVars.
    for (const auto& child : mod.children) {
        if (child) walkNode(*child, moduleScope, analysis_, nullptr, &classes_);
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
        auto classNameIdx = m.internSymbol(n.text);

        // T3-b: multiple inheritance / mixins. A `uses: { … }` clause is
        // stored as the ClassDecl node's single child (an expression yielding
        // a collection of class objects). When present, the new class must be
        // assembled with ALL its parents baked into the prototype chain BEFORE
        // any instance is created — protoCore freezes an object's parent chain
        // at construction, so a parent added afterwards is invisible to
        // instances. The class is therefore built by the `subclass:uses:`
        // runtime primitive, which assembles the full chain in one shot.
        // The textual form desugars to a genuine message send to the
        // superclass: `superGlobal subclass: #Name uses: <mixins>`.
        if (!n.children.empty() && n.children[0]) {
            auto superIdx = m.internSymbol(superName);
            m.emitWide(Op::PUSH_GLOBAL,
                       static_cast<unsigned int>(superIdx), currentLine_);
            // arg 0: the class name (a symbol literal — emitted exactly as a
            // SymbolLit node would: an interned symbol pushed via PUSH_CONST).
            auto nameSymIdx = m.internSymbol(n.text);
            m.emitWide(Op::PUSH_CONST,
                       static_cast<unsigned int>(nameSymIdx), currentLine_);
            std::string selector;
            if (n.stringList.size() > 1) {
                // declared instance variables → subclass:instanceVariableNames:uses:
                std::string ivars;
                for (size_t i = 1; i < n.stringList.size(); ++i) {
                    if (i > 1) ivars += ' ';
                    ivars += n.stringList[i];
                }
                auto ivStrIdx = m.addString(ivars);
                m.emitWide(Op::PUSH_CONST,
                           static_cast<unsigned int>(ivStrIdx), currentLine_);
                selector = "subclass:instanceVariableNames:uses:";
            } else {
                selector = "subclass:uses:";
            }
            // final arg: the mixin collection expression
            emitExpr(m, *n.children[0]);
            auto selIdx = m.internSymbol(selector);
            m.emitWide(Op::SEND_KEYWORD,
                       static_cast<unsigned int>(selIdx), currentLine_);
            // The primitive already binds the class as a global under its
            // name; DUP keeps it on the stack as this statement's value and
            // STORE_GLOBAL rebinds it (idempotent), matching the plain form.
            m.emit(Op::DUP, 0, currentLine_);
            m.emitWide(Op::STORE_GLOBAL,
                       static_cast<unsigned int>(classNameIdx), currentLine_);
            return;
        }

        auto superIdx     = m.internSymbol(superName);
        auto newChildIdx  = m.internSymbol("newChild");
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
        // symbol, send the install selector to the class. The send returns the
        // class (the receiver), leaving it on the stack. The surrounding module
        // loop's POP separator (or RETURN_TOP) consumes it cleanly.
        //
        // D5 (MNT-b2): an instance-side method is installed via
        // `__installMethod:as:`; a `ClassName class >> sel` (n.boolFlag) via
        // `__installClassMethod:as:`, which additionally stamps the method
        // wrapper with `__class_side__`. Both still install onto the SAME class
        // object — the metamodel is unchanged — but the marker lets the engine's
        // SEND dispatch refuse a class-side method when the receiver is an
        // instance (not a class), so class-side and instance-side protocols are
        // no longer reachable from the same receiver.
        auto classIdx    = m.internSymbol(n.text);
        auto selectorIdx = m.internSymbol(n.stringList[0]);
        auto installIdx  = m.internSymbol(
            n.boolFlag ? "__installClassMethod:as:" : "__installMethod:as:");

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
            // 2026-05-24: doYielding: compiler-desugar. When the source
            // is exactly `<receiver> doYielding: [ :elem | <body> ]`
            // (one keyword, one arg, the arg a literal one-parameter
            // block), emit a bytecode loop using `at:` + `value:`
            // instead of dispatching `doYielding:` as a normal SEND.
            // The block's `value:` send hits the engine's inline
            // block-frame fast path (yieldable), so a `wait` inside
            // the block parks correctly mid-iteration.
            //
            // Receivers that do not support `at:` and `size` will
            // surface doesNotUnderstand at runtime — fail-fast, by
            // design. `do:` itself is untouched: callers using `do:`
            // get the unchanged polymorphic primitive.
            //
            // Any non-matching shape (variable as block, wrong
            // arity, etc.) falls through to the normal SEND_KEYWORD
            // path. Since no primitive is bound for `doYielding:`,
            // that surfaces doesNotUnderstand at runtime.
            //
            // See docs/superpowers/specs/2026-05-24-doyielding-design.md.
            if (n.text == "doYielding:"
                && n.children.size() == 2
                && n.children[1]->kind == NodeKind::Block
                && n.children[1]->intValue == 1) {
                emitDoYieldingLoop(m, *n.children[0], *n.children[1]);
                return;
            }
            // 2026-05-24 Tier-S: inline ifTrue:/ifFalse:/ifTrue:ifFalse:/
            // whileTrue:/whileFalse: when their block argument(s) are
            // zero-arg block literals with no local declarations. See
            // the helper for why.
            if (tryEmitInlinedControl(m, n)) return;
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
        case NodeKind::ArrayLit:
        case NodeKind::DynArrayLit: {
            // COL-a: collection literals lower to a sequence of element
            // pushes followed by MAKE_ARRAY N. The MAKE_ARRAY engine handler
            // pops the N values (oldest-first → element 0) and pushes a fresh
            // Array instance.
            //
            //   #(...) — the parser only admits compile-time literals
            //            (IntegerLit/FloatLit/StringLit/SymbolLit; a bare
            //            identifier was already turned into a SymbolLit). Each
            //            child is therefore an ordinary literal expression and
            //            emitExpr emits a single PUSH_CONST for it.
            //   {...}  — each child is an arbitrary expression evaluated at
            //            runtime; emitExpr emits whatever it needs.
            for (const auto& child : n.children) {
                if (child) emitExpr(m, *child);
            }
            m.emitWide(Op::MAKE_ARRAY,
                       static_cast<unsigned int>(n.children.size()),
                       currentLine_);
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

// 2026-05-24: Emit the bytecode loop for `<recv> doYielding: [ :elem | body ]`.
//
// Layout (offsets in 2-byte instruction units; assumes no EXTEND-widened
// opcodes in the loop body — see "Constraints" below):
//
//   [ receiver expr ... ]
//   STORE_LOCAL coll                    ; cache receiver in a fresh local
//   PUSH_LOCAL coll
//   SEND_UNARY size
//   STORE_LOCAL n                       ; n := coll size
//   PUSH_CONST 1                        ; (1 added to constant pool)
//   STORE_LOCAL i
// LOOP_TEST:
//   PUSH_LOCAL i
//   PUSH_LOCAL n
//   SEND_BINARY <=
//   JUMP_IF_FALSE -> LOOP_END           ; patched once body length is known
//   PUSH_LOCAL coll
//   PUSH_LOCAL i
//   SEND_KEYWORD at:
//   [ block expr ... ]                  ; emits PUSH_BLOCK
//   SEND_KEYWORD value:                 ; yieldable — engine inline frame push
//   POP                                  ; discard block result
//   PUSH_LOCAL i
//   PUSH_CONST 1
//   SEND_BINARY +
//   STORE_LOCAL i
//   JUMP_BACK -> LOOP_TEST              ; backward offset
// LOOP_END:
//   PUSH_LOCAL coll                     ; do:'s return value is the receiver
//
// Constraints (v1):
//   * The forward JUMP_IF_FALSE and the backward JUMP_BACK use 8-bit args.
//     If the body grows past 255 instructions, the compiler will detect it
//     (`bytePos` arithmetic) and surface an error. Both offsets are well
//     under that bound for realistic loop bodies.
//   * `instrStartPc()` is consulted to convert byte positions back into
//     instruction indices — that handles any EXTEND prefixes inside the
//     body's nested expressions (e.g. a STORE_LOCAL with slot index > 255
//     in the block body) by counting them as one logical instruction.
void Compiler::emitDoYieldingLoop(BytecodeModule& m,
                                  const ast::Node& receiverNode,
                                  const ast::Node& blockNode) {
    // Reserve fresh locals — mangled with the next-slot count so each
    // nested doYielding: in the same scope gets distinct names.
    int baseSlot = scopes_.back().nextSlot;
    int slotColl = declareLocal("__dy_coll_" + std::to_string(baseSlot));
    int slotBlk  = declareLocal("__dy_blk_"  + std::to_string(baseSlot));
    int slotN    = declareLocal("__dy_n_"    + std::to_string(baseSlot));
    int slotI    = declareLocal("__dy_i_"    + std::to_string(baseSlot));

    // The interned `1` constant — reused for the i++ at loop tail.
    size_t oneConstIdx = m.addInteger(1);

    // Helper: current instruction index (count of words already emitted).
    auto curInstrIdx = [&]() -> size_t {
        return m.instrStartPc().size();
    };

    // ---- Setup: coll := receiver; blk := the block (hoisted, 1 PUSH_BLOCK
    // for the whole loop instead of one per iter); n := coll size; i := 1.
    emitExpr(m, receiverNode);
    m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotColl), currentLine_);
    emitExpr(m, blockNode);  // → PUSH_BLOCK
    m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotBlk),  currentLine_);
    m.emitWide(Op::PUSH_LOCAL,  static_cast<unsigned int>(slotColl), currentLine_);
    m.emitWide(Op::SEND_UNARY,  static_cast<unsigned int>(m.internSymbol("size")), currentLine_);
    m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotN),    currentLine_);
    m.emitWide(Op::PUSH_CONST,  static_cast<unsigned int>(oneConstIdx), currentLine_);
    m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotI),    currentLine_);

    // ---- LOOP_TEST:
    size_t loopTestInstrIdx = curInstrIdx();

    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotI), currentLine_);
    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotN), currentLine_);
    m.emitWide(Op::SEND_BINARY,
               static_cast<unsigned int>(m.internSymbol("<=")), currentLine_);

    // JUMP_IF_FALSE — placeholder, patched below. emit() (not emitWide()) so
    // this is exactly one 2-byte word and the arg byte sits at bytes()[N+1].
    size_t jumpFalseInstrIdx = curInstrIdx();
    size_t jumpFalseBytePos  = m.bytes().size();
    m.emit(Op::JUMP_IF_FALSE, 0, currentLine_);

    // ---- Body:  blk value: (coll at: i)
    // Stack discipline: SEND_KEYWORD `value:` pops 1 arg then receiver from
    // top — so receiver (the block) must be pushed FIRST, then the arg.
    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotBlk),  currentLine_);
    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotColl), currentLine_);
    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotI),    currentLine_);
    m.emitWide(Op::SEND_KEYWORD,
               static_cast<unsigned int>(m.internSymbol("at:")), currentLine_);
    m.emitWide(Op::SEND_KEYWORD,
               static_cast<unsigned int>(m.internSymbol("value:")), currentLine_);
    m.emit(Op::POP, 0, currentLine_);

    // ---- Increment: i := i + 1
    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotI), currentLine_);
    m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(oneConstIdx), currentLine_);
    m.emitWide(Op::SEND_BINARY,
               static_cast<unsigned int>(m.internSymbol("+")), currentLine_);
    m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotI), currentLine_);

    // ---- JUMP_BACK to LOOP_TEST.
    // The handler does f.pc -= arg * kInstrSize AFTER pc has already been
    // advanced past this JUMP_BACK word. So the arg = number of full
    // instructions from LOOP_TEST up to and INCLUDING this JUMP_BACK.
    size_t jumpBackInstrIdx = curInstrIdx();
    size_t backOffset = jumpBackInstrIdx - loopTestInstrIdx + 1;
    if (backOffset > 255) {
        error("doYielding: loop body too large for 8-bit JUMP_BACK offset");
        return;
    }
    m.emit(Op::JUMP_BACK, static_cast<uint8_t>(backOffset), currentLine_);

    // ---- LOOP_END: patch the JUMP_IF_FALSE forward offset.
    // JUMP_IF_FALSE's pc advance happens BEFORE the arg shift; arg = number
    // of instructions to skip forward from the slot RIGHT AFTER
    // JUMP_IF_FALSE.
    size_t loopEndInstrIdx = curInstrIdx();
    size_t fwdOffset = loopEndInstrIdx - jumpFalseInstrIdx - 1;
    if (fwdOffset > 255) {
        error("doYielding: loop body too large for 8-bit JUMP_IF_FALSE offset");
        return;
    }
    m.patchArg(jumpFalseBytePos, static_cast<uint8_t>(fwdOffset));

    // ---- do:'s return value is the receiver.
    m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotColl), currentLine_);
}

// 2026-05-24 Tier-S perf: inline ifTrue:/ifFalse:/ifTrue:ifFalse: and
// whileTrue:/whileFalse: when their argument(s) are zero-arg, no-local
// block literals. The win is structural, not micro: each non-inlined
// `cond ifTrue: [^expr]` runs the block through `prim_True_ifTrue` →
// `invokeBlock` → a NESTED ExecutionEngine on the C++ stack, and an
// `^expr` from inside that block throws NonLocalReturn to unwind the
// nested engine back to the parent runLoop. On fib(25) that is ~75K
// C++ throws (one per leaf call), each one walking the personality
// routine and the unwind FDE tables — measured at ~11 % of fib CPU
// before this inlining. Inlining the body emits its instructions into
// the current frame so `^expr` becomes a plain local RETURN with no
// stack unwind.
//
// We bail out (return false) if the block has any args or any local
// declarations — closure-analysis decisions cannot be retraced
// post-hoc, and captured variables would need separate plumbing. The
// caller falls back to a normal SEND_KEYWORD when inlining declines.
bool Compiler::tryEmitInlinedControl(BytecodeModule& m, const ast::Node& n) {
    using ast::NodeKind;
    auto isInlineableBlock = [](const ast::Node* b) -> bool {
        return b && b->kind == NodeKind::Block
            && b->intValue == 0
            && b->stringList.empty();
    };

    // cond ifTrue:  [body]  / cond ifFalse: [body]
    if (n.children.size() == 2
        && isInlineableBlock(n.children[1].get())
        && (n.text == "ifTrue:" || n.text == "ifFalse:")) {
        bool invert = (n.text == "ifFalse:");
        emitExpr(m, *n.children[0]);
        // Skip body if the cond doesn't match the polarity we want.
        // JUMP_IF_FALSE skips when receiver is PROTO_FALSE (so it skips
        // the body of ifTrue when cond is false). JUMP_IF_TRUE skips
        // when receiver is PROTO_TRUE (mirror for ifFalse).
        size_t jSkipBytePos  = m.bytes().size();
        size_t jSkipInstrIdx = m.instrStartPc().size();
        m.emit(invert ? Op::JUMP_IF_TRUE : Op::JUMP_IF_FALSE, 0, currentLine_);
        emitInlinedBlockBody(m, *n.children[1]);
        // The inlined body left a result on the stack. Jump past the nil
        // we are about to emit for the not-taken branch.
        size_t jDoneBytePos  = m.bytes().size();
        size_t jDoneInstrIdx = m.instrStartPc().size();
        m.emit(Op::JUMP, 0, currentLine_);
        // Patch jSkip target = right here (PUSH_NIL is next).
        size_t skipTargetIdx = m.instrStartPc().size();
        size_t skipOffset    = skipTargetIdx - jSkipInstrIdx - 1;
        if (skipOffset > 255) { error("inline if: block too large"); return false; }
        m.patchArg(jSkipBytePos, static_cast<uint8_t>(skipOffset));
        m.emit(Op::PUSH_NIL, 0, currentLine_);
        // Patch jDone target = right here.
        size_t doneTargetIdx = m.instrStartPc().size();
        size_t doneOffset    = doneTargetIdx - jDoneInstrIdx - 1;
        if (doneOffset > 255) { error("inline if: block too large"); return false; }
        m.patchArg(jDoneBytePos, static_cast<uint8_t>(doneOffset));
        return true;
    }

    // cond ifTrue: [t] ifFalse: [f]  / cond ifFalse: [f] ifTrue: [t]
    if (n.children.size() == 3
        && isInlineableBlock(n.children[1].get())
        && isInlineableBlock(n.children[2].get())
        && (n.text == "ifTrue:ifFalse:" || n.text == "ifFalse:ifTrue:")) {
        bool ifFalseFirst = (n.text == "ifFalse:ifTrue:");
        const ast::Node& trueBranch  = ifFalseFirst ? *n.children[2] : *n.children[1];
        const ast::Node& falseBranch = ifFalseFirst ? *n.children[1] : *n.children[2];
        emitExpr(m, *n.children[0]);
        // Jump to the false branch if cond is false.
        size_t jToFalseBytePos  = m.bytes().size();
        size_t jToFalseInstrIdx = m.instrStartPc().size();
        m.emit(Op::JUMP_IF_FALSE, 0, currentLine_);
        // True branch.
        emitInlinedBlockBody(m, trueBranch);
        size_t jDoneBytePos  = m.bytes().size();
        size_t jDoneInstrIdx = m.instrStartPc().size();
        m.emit(Op::JUMP, 0, currentLine_);
        // False branch starts here.
        size_t falseTargetIdx = m.instrStartPc().size();
        size_t toFalseOffset  = falseTargetIdx - jToFalseInstrIdx - 1;
        if (toFalseOffset > 255) { error("inline if/else: block too large"); return false; }
        m.patchArg(jToFalseBytePos, static_cast<uint8_t>(toFalseOffset));
        emitInlinedBlockBody(m, falseBranch);
        // Done marker.
        size_t doneTargetIdx = m.instrStartPc().size();
        size_t doneOffset    = doneTargetIdx - jDoneInstrIdx - 1;
        if (doneOffset > 255) { error("inline if/else: block too large"); return false; }
        m.patchArg(jDoneBytePos, static_cast<uint8_t>(doneOffset));
        return true;
    }

    // start to: end do: [:i | body]
    // The block has exactly one argument and no local declarations; its
    // argument MUST NOT be captured by any inner block (otherwise the
    // captured-dict plumbing would need separate setup we cannot do
    // post-analysis). int_sum_loop's `1 to: 100000 do: [:i| total := total
    // + i]` matches this pattern; the inlining replaces the
    // PUSH_BLOCK + SEND_KEYWORD + prim_Integer_toDo:do: + invokeBlock
    // chain with a direct bytecode loop using SmallInt arithmetic.
    if (n.text == "to:do:"
        && n.children.size() == 3
        && n.children[2]->kind == ast::NodeKind::Block
        && n.children[2]->intValue == 1
        && n.children[2]->stringList.size() == 1) {
        const ast::Node* blk = n.children[2].get();
        auto capIt = analysis_.capturedByScope.find(blk);
        bool argIsCaptured = (capIt != analysis_.capturedByScope.end()
                              && capIt->second.count(blk->stringList[0]) != 0);
        if (!argIsCaptured) {
            // Reserve fresh internal slots — mangled with nextSlot so nested
            // to:do: in the same scope produce distinct names.
            auto& s = scopes_.back();
            int baseSlot = s.nextSlot;
            int slotI   = declareLocal("__td_i_"   + std::to_string(baseSlot));
            int slotEnd = declareLocal("__td_end_" + std::to_string(baseSlot));
            // Setup: i := start; end := endExpr.
            emitExpr(m, *n.children[0]);
            m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotI), currentLine_);
            emitExpr(m, *n.children[1]);
            m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotEnd), currentLine_);
            // Temporarily rebind the user's iter-var name to our slot so
            // body references resolve correctly.
            const std::string& iterName = blk->stringList[0];
            auto savedIt = s.slots.find(iterName);
            int savedSlot = (savedIt != s.slots.end()) ? savedIt->second : -1;
            bool hadBinding = (savedIt != s.slots.end());
            s.slots[iterName] = slotI;
            // loopTest:
            size_t loopTestInstrIdx = m.instrStartPc().size();
            m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotI),   currentLine_);
            m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotEnd), currentLine_);
            m.emitWide(Op::SEND_BINARY,
                       static_cast<unsigned int>(m.internSymbol("<=")), currentLine_);
            size_t jExitBytePos  = m.bytes().size();
            size_t jExitInstrIdx = m.instrStartPc().size();
            m.emit(Op::JUMP_IF_FALSE, 0, currentLine_);
            // body (the block's children) — inlined.
            emitInlinedBlockBody(m, *blk);
            m.emit(Op::POP, 0, currentLine_);
            // i := i + 1
            size_t oneConstIdx = m.addInteger(1);
            m.emitWide(Op::PUSH_LOCAL, static_cast<unsigned int>(slotI), currentLine_);
            m.emitWide(Op::PUSH_CONST, static_cast<unsigned int>(oneConstIdx), currentLine_);
            m.emitWide(Op::SEND_BINARY,
                       static_cast<unsigned int>(m.internSymbol("+")), currentLine_);
            m.emitWide(Op::STORE_LOCAL, static_cast<unsigned int>(slotI), currentLine_);
            // JUMP_BACK loopTest
            size_t jBackInstrIdx = m.instrStartPc().size();
            size_t backOffset    = jBackInstrIdx - loopTestInstrIdx + 1;
            if (backOffset > 255) { error("inline to:do:: body too large"); return false; }
            m.emit(Op::JUMP_BACK, static_cast<uint8_t>(backOffset), currentLine_);
            // exit target
            size_t exitTargetIdx = m.instrStartPc().size();
            size_t exitOffset    = exitTargetIdx - jExitInstrIdx - 1;
            if (exitOffset > 255) { error("inline to:do:: body too large"); return false; }
            m.patchArg(jExitBytePos, static_cast<uint8_t>(exitOffset));
            // Restore the user's iter-var binding.
            if (hadBinding) s.slots[iterName] = savedSlot;
            else            s.slots.erase(iterName);
            // to:do: returns the receiver (the start integer); push it.
            emitExpr(m, *n.children[0]);
            return true;
        }
    }

    // [cond] whileTrue: [body]  / [cond] whileFalse: [body]
    if (n.children.size() == 2
        && isInlineableBlock(n.children[0].get())
        && isInlineableBlock(n.children[1].get())
        && (n.text == "whileTrue:" || n.text == "whileFalse:")) {
        bool invert = (n.text == "whileFalse:");
        // loopTest:
        size_t loopTestInstrIdx = m.instrStartPc().size();
        emitInlinedBlockBody(m, *n.children[0]);  // cond → top of stack
        // Exit loop when cond does NOT match polarity.
        size_t jExitBytePos  = m.bytes().size();
        size_t jExitInstrIdx = m.instrStartPc().size();
        m.emit(invert ? Op::JUMP_IF_TRUE : Op::JUMP_IF_FALSE, 0, currentLine_);
        // body, discard its value, loop back.
        emitInlinedBlockBody(m, *n.children[1]);
        m.emit(Op::POP, 0, currentLine_);
        size_t jBackInstrIdx = m.instrStartPc().size();
        size_t backOffset    = jBackInstrIdx - loopTestInstrIdx + 1;
        if (backOffset > 255) { error("inline while: body too large"); return false; }
        m.emit(Op::JUMP_BACK, static_cast<uint8_t>(backOffset), currentLine_);
        // exit target — whileTrue: returns nil.
        size_t exitTargetIdx = m.instrStartPc().size();
        size_t exitOffset    = exitTargetIdx - jExitInstrIdx - 1;
        if (exitOffset > 255) { error("inline while: body too large"); return false; }
        m.patchArg(jExitBytePos, static_cast<uint8_t>(exitOffset));
        m.emit(Op::PUSH_NIL, 0, currentLine_);
        return true;
    }

    return false;
}

void Compiler::emitInlinedBlockBody(BytecodeModule& m, const ast::Node& block) {
    if (block.children.empty()) {
        m.emit(Op::PUSH_NIL, 0, currentLine_);
        return;
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        emitStatement(m, *block.children[i]);
        if (i + 1 != block.children.size()) m.emit(Op::POP, 0, currentLine_);
    }
}

void Compiler::patchJumpToHere(BytecodeModule& m, std::size_t /*jumpInstrPos*/) {
    // Kept for forward compatibility — the inline helpers above currently
    // patch jumps directly via m.patchArg + m.instrStartPc() indexing.
}

void Compiler::error(const std::string& msg) { errors_.push_back(msg); }

} // namespace protoST
