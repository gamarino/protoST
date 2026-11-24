// Instance-variable modifiers (explicit-state spec §6.1): the
// constantNames:, writeOnceNames: and atomicNames: clauses of a class
// declaration, the records the compiler keeps for them, and the helpers of
// src/runtime/IvarModifiers.h.

#include <catch2/catch_all.hpp>

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "frontend/Compiler.h"
#include "protoST/STRuntime.h"
#include "runtime/NativeError.h"
#include "protoCore.h"
#include "frontend/Parser.h"
#include "runtime/IvarModifiers.h"
#include "runtime/BlockCode.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"
#include "runtime/Bootstrap.h"
#include "primitives/state_prims.h"

using protoST::IvarModifier;
using protoST::ast::NodeKind;

namespace {

const protoST::ast::Node& onlyClassDecl(const protoST::ast::Node& mod) {
    REQUIRE(mod.children.size() == 1);
    REQUIRE(mod.children[0]->kind == NodeKind::ClassDecl);
    return *mod.children[0];
}

std::vector<std::string> ivarNamesOf(const protoST::ast::Node& cd) {
    return {cd.stringList.begin() + 1, cd.stringList.begin() + 1 + cd.intValue};
}

} // namespace

TEST_CASE("IvarModifiers: the four clauses are parsed in source order", "[ivar_modifiers]") {
    protoST::Parser P(
        "Object subclass: #Account instanceVariableNames: 'history' constantNames: 'number owner' "
        "writeOnceNames: 'oid' atomicNames: 'balance'.");
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    const auto& cd = onlyClassDecl(*mod);
    CHECK(cd.stringList == std::vector<std::string>{"Object", "history", "number", "owner", "oid", "balance"});
    CHECK(cd.intValue == 5);
    CHECK(cd.ivarModifiers == std::vector<IvarModifier>{IvarModifier::Plain, IvarModifier::Constant,
                                                       IvarModifier::Constant, IvarModifier::WriteOnce,
                                                       IvarModifier::Atomic});
}

TEST_CASE("IvarModifiers: clauses in any order keep source order", "[ivar_modifiers]") {
    protoST::Parser P(
        "Object subclass: #Account atomicNames: 'balance' classVariableNames: 'Count' "
        "constantNames: 'number owner' instanceVariableNames: 'history'.");
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    const auto& cd = onlyClassDecl(*mod);
    CHECK(ivarNamesOf(cd) == std::vector<std::string>{"balance", "number", "owner", "history"});
    CHECK(cd.stringList.back() == "Count");
    CHECK(cd.ivarModifiers == std::vector<IvarModifier>{IvarModifier::Atomic, IvarModifier::Constant,
                                                       IvarModifier::Constant, IvarModifier::Plain});
}

TEST_CASE("IvarModifiers: a name in two clauses is declared twice", "[ivar_modifiers]") {
    protoST::Parser P(
        "Object subclass: #Account writeOnceNames: 'oid' atomicNames: 'oid'.");
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    C.compileModule(*mod);
    REQUIRE(C.hasErrors());
    CHECK(C.errors().front().find("instance variable 'oid' is declared twice in Account")
          != std::string::npos);
}

TEST_CASE("IvarModifiers: modifier names and the generated constructor selector", "[ivar_modifiers]") {
    CHECK(std::string(protoST::modifierName(IvarModifier::Plain)) == "plain");
    CHECK(std::string(protoST::modifierName(IvarModifier::Constant)) == "constant");
    CHECK(std::string(protoST::modifierName(IvarModifier::WriteOnce)) == "writeOnce");
    CHECK(std::string(protoST::modifierName(IvarModifier::Atomic)) == "atomic");
    IvarModifier m = IvarModifier::Plain;
    CHECK(protoST::parseModifierName("writeOnce", m));
    CHECK(m == IvarModifier::WriteOnce);
    CHECK_FALSE(protoST::parseModifierName("field", m));
    CHECK(protoST::constructorSelectorFor({"number", "owner"}) == "number:owner:");
    CHECK(protoST::constructorSelectorFor({}).empty());
}

TEST_CASE("IvarModifiers: the compiler records each class's own modifiers and resolves inherited ones",
          "[ivar_modifiers]") {
    protoST::Parser P(
        "Object subclass: #IvmBase instanceVariableNames: 'h' constantNames: 'number' atomicNames: 'balance'.\n"
        "Object subclass: #IvmMixin writeOnceNames: 'oid'.\n"
        "IvmBase subclass: #IvmSub instanceVariableNames: 'x' uses: { IvmMixin }.\n");
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    C.compileModule(*mod);
    REQUIRE_FALSE(C.hasErrors());
    const auto& base = C.classes().at("IvmBase");
    CHECK(base.modifiers.size() == 2);   // plain variables are not recorded
    CHECK(base.modifiers.at("number") == IvarModifier::Constant);
    CHECK(base.modifiers.at("balance") == IvarModifier::Atomic);
    CHECK(C.classes().at("IvmSub").modifiers.empty());
    CHECK(C.modifierThroughChain("IvmSub", "balance") == IvarModifier::Atomic);
    CHECK(C.modifierThroughChain("IvmSub", "oid") == IvarModifier::WriteOnce);
    CHECK(C.modifierThroughChain("IvmSub", "h") == IvarModifier::Plain);
    CHECK(C.modifierThroughChain("IvmSub", "x") == IvarModifier::Plain);
}

// Review focus 4: a class declared in one REPL input (or module) and a method
// of it compiled in a later one. The compiler must see the modifiers through
// knownClasses_ (REPL) and through the shared class registry (an imported
// module).
TEST_CASE("IvarModifiers: modifiers survive into a later compilation", "[ivar_modifiers]") {
    protoST::Parser P1("Object subclass: #IvmAccount atomicNames: 'balance'.");
    auto mod1 = P1.parseModule();
    REQUIRE(P1.errors().empty());
    protoST::Compiler first;
    first.setReplMode(true);
    first.compileModule(*mod1);
    REQUIRE_FALSE(first.hasErrors());
    REQUIRE(first.classes().at("IvmAccount").modifiers.at("balance") == IvarModifier::Atomic);

    // The REPL path: the next input's compiler is given the session classes.
    protoST::Compiler second;
    second.setReplMode(true);
    second.setKnownClasses(first.classes());
    CHECK(second.modifierThroughChain("IvmAccount", "balance") == IvarModifier::Atomic);

    // The module path: collectClasses published the class to the registry.
    const auto snapshot = protoST::Compiler::sharedClassRegistrySnapshot();
    REQUIRE(snapshot.count("IvmAccount") == 1);
    CHECK(snapshot.at("IvmAccount").modifiers.at("balance") == IvarModifier::Atomic);
    protoST::Compiler third;
    CHECK(third.modifierThroughChain("IvmAccount", "balance") == IvarModifier::Atomic);
}

// The second half of review focus 4: the later method is refused with M4.
TEST_CASE("IvarModifiers: a later REPL method that assigns an atomic variable reports M4",
          "[ivar_modifiers]") {
    protoST::Parser P1("Object subclass: #IvmAccount atomicNames: 'balance'.");
    auto mod1 = P1.parseModule();
    REQUIRE(P1.errors().empty());
    protoST::Compiler first;
    first.setReplMode(true);
    first.compileModule(*mod1);
    REQUIRE_FALSE(first.hasErrors());

    protoST::Parser P2("IvmAccount >> f\n  balance := 1.\n");
    auto mod2 = P2.parseModule();
    REQUIRE(P2.errors().empty());
    protoST::Compiler second;
    second.setReplMode(true);
    second.setKnownClasses(first.classes());
    second.compileModule(*mod2);
    REQUIRE(second.hasErrors());
    CHECK(second.errors().front().find(
              "cannot assign to the atomic variable 'balance' of IvmAccount: "
              "use update:with: or compareAndSet:from:to:")
          != std::string::npos);
}

// The run-time lookups of IvarModifiers.h, on classes built by a program.
TEST_CASE("IvarModifiers: run-time lookups on classes, instances and class objects", "[ivar_modifiers]") {
    const char* src =
        "Object subclass: #CnBase constantNames: 'a' instanceVariableNames: 'p'.\n"
        "Object subclass: #CnMix atomicNames: 'n' constantNames: 'm'.\n"
        "CnBase subclass: #CnKid constantNames: 'k' writeOnceNames: 'w' uses: { CnMix }.\n"
        "CnKid class instanceVariableNames: 'count'.\n"
        "Smalltalk at: #CnInstance put: CnKid basicNew.\n"
        "0\n";
    protoST::Parser P(src);
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*mod);
    REQUIRE_FALSE(C.hasErrors());
    protoST::STRuntime rt;
    std::string error;
    try {
        rt.runTopLevel(*bc);
    } catch (const std::exception& e) {
        error = protoST::nativeErrorText(e);
    }
    REQUIRE(error.empty());
    proto::ProtoContext* ctx = rt.rootCtx();
    auto global = [&](const char* name) {
        return rt.globals()->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
    };
    const proto::ProtoObject* kid = global("CnKid");
    const proto::ProtoObject* inst = global("CnInstance");
    REQUIRE(kid != nullptr);
    REQUIRE(inst != nullptr);

    // Inherited first: the superclass's, the mixin's, the class's own.
    const auto constants = protoST::constantNamesOf(rt, ctx, kid);
    CHECK(constants == std::vector<std::string>{"a", "m", "k"});
    CHECK(protoST::constructorSelectorFor(constants) == "a:m:k:");
    CHECK(protoST::constantNamesOf(rt, ctx, global("CnMix")) == std::vector<std::string>{"m"});

    bool declared = false;
    CHECK(protoST::modifierOf(rt, ctx, kid, "n", &declared) == IvarModifier::Atomic);
    CHECK(declared);
    CHECK(protoST::modifierOf(rt, ctx, inst, "w", &declared) == IvarModifier::WriteOnce);
    CHECK(declared);
    CHECK(protoST::modifierOf(rt, ctx, inst, "p", &declared) == IvarModifier::Plain);
    CHECK(declared);
    CHECK(protoST::modifierOf(rt, ctx, inst, "zork", &declared) == IvarModifier::Plain);
    CHECK_FALSE(declared);

    // Ruling C10: a class object's own variables are its class-instance
    // variables, always plain.
    CHECK(protoST::declaresInstVar(rt, ctx, inst, "a"));
    CHECK_FALSE(protoST::declaresInstVar(rt, ctx, inst, "count"));
    CHECK(protoST::declaresInstVar(rt, ctx, kid, "count"));
    CHECK_FALSE(protoST::declaresInstVar(rt, ctx, kid, "a"));
    CHECK(protoST::receiverModifierOf(rt, ctx, kid, "count", &declared) == IvarModifier::Plain);
    CHECK(declared);
    CHECK(protoST::receiverModifierOf(rt, ctx, kid, "n", &declared) == IvarModifier::Plain);
    CHECK_FALSE(declared);
    CHECK(protoST::receiverModifierOf(rt, ctx, inst, "n", &declared) == IvarModifier::Atomic);
    CHECK(declared);

    // Non-objects declare nothing.
    CHECK(protoST::modifierOf(rt, ctx, ctx->fromLong(3), "a", &declared) == IvarModifier::Plain);
    CHECK_FALSE(declared);
    CHECK_FALSE(protoST::declaresInstVar(rt, ctx, PROTO_NONE, "a"));
}

// `__has_constants__` answers, through the chain, whether a class declares or
// inherits a constant (plan decision D-a). A class that drops its own
// constants must not keep an own record that hides one its superclass gains
// later (fix round 1 of Task 2; Task 6's M13 check reads it).
TEST_CASE("IvarModifiers: has-constants follows the chain after redeclarations", "[ivar_modifiers]") {
    const char* src =
        "Object subclass: #HcBase instanceVariableNames: 'p'.\n"
        "HcBase subclass: #HcSub constantNames: 'k'.\n"
        "HcBase subclass: #HcSub instanceVariableNames: 'q'.\n"
        "Object subclass: #HcPlain instanceVariableNames: 'r'.\n"
        "Object subclass: #HcBase instanceVariableNames: 'p' constantNames: 'c'.\n"
        "0\n";
    protoST::Parser P(src);
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*mod);
    REQUIRE_FALSE(C.hasErrors());
    protoST::STRuntime rt;
    std::string error;
    try {
        rt.runTopLevel(*bc);
    } catch (const std::exception& e) {
        error = protoST::nativeErrorText(e);
    }
    REQUIRE(error.empty());
    proto::ProtoContext* ctx = rt.rootCtx();
    auto global = [&](const char* name) {
        return rt.globals()->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
    };
    const proto::ProtoString* key = proto::ProtoString::createSymbol(ctx, "__has_constants__");
    const proto::ProtoObject* sub = global("HcSub");
    const proto::ProtoObject* base = global("HcBase");
    REQUIRE(sub != nullptr);
    REQUIRE(base != nullptr);
    CHECK(base->getAttribute(ctx, key) == PROTO_TRUE);
    CHECK(sub->getAttribute(ctx, key) == PROTO_TRUE);
    CHECK(sub->hasOwnAttribute(ctx, key) != PROTO_TRUE);
    CHECK(global("HcPlain")->getAttribute(ctx, key) != PROTO_TRUE);
    CHECK(protoST::constantNamesOf(rt, ctx, sub) == std::vector<std::string>{"c"});
}

// ---------------------------------------------------------------------------
// Assignment rules (explicit-state spec §6.2, §6.4, §6.5; Task 3)
// ---------------------------------------------------------------------------

namespace {

// The opcodes of `m` in code order.
std::vector<protoST::Op> opsOf(const protoST::BytecodeModule& m) {
    std::vector<protoST::Op> ops;
    const auto& b = m.bytes();
    for (std::size_t pc = 0; pc + 1 < b.size();) {
        auto op = static_cast<protoST::Op>(b[pc]);
        pc += protoST::kInstrSize;
        while (op == protoST::Op::EXTEND && pc + 1 < b.size()) {
            op = static_cast<protoST::Op>(b[pc]);
            pc += protoST::kInstrSize;
        }
        ops.push_back(op);
    }
    return ops;
}

// Every module of the tree rooted at `m`, `m` first.
void modulesOf(const protoST::BytecodeModule& m, std::vector<const protoST::BytecodeModule*>& out) {
    out.push_back(&m);
    for (std::size_t i = 0; i < m.numBlocks(); ++i) modulesOf(m.block(i), out);
}

// How many times `op` occurs in the whole tree.
int countOp(const protoST::BytecodeModule& root, protoST::Op op) {
    std::vector<const protoST::BytecodeModule*> mods;
    modulesOf(root, mods);
    int n = 0;
    for (const auto* mod : mods)
        for (protoST::Op o : opsOf(*mod)) n += o == op ? 1 : 0;
    return n;
}

struct Compiled {
    std::unique_ptr<protoST::BytecodeModule> code;
    std::vector<std::string> errors;
};

Compiled compileSource(const std::string& src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    Compiled out;
    out.code = C.compileModule(*ast);
    out.errors = C.errors();
    return out;
}

bool hasErrorContaining(const Compiled& c, const std::string& text) {
    for (const auto& e : c.errors)
        if (e.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

TEST_CASE("IvarModifiers: a constant assigned in initialize compiles to STORE_CONST_INSTVAR",
          "[ivar_modifiers]") {
    const auto c = compileSource(
        "Object subclass: #IvmConstA instanceVariableNames: 'history' constantNames: 'number'.\n"
        "IvmConstA >> initialize\n"
        "  number := 7\n");
    REQUIRE(c.errors.empty());
    CHECK(countOp(*c.code, protoST::Op::STORE_CONST_INSTVAR) == 1);
    CHECK(countOp(*c.code, protoST::Op::STORE_INSTVAR) == 0);
    CHECK(countOp(*c.code, protoST::Op::IVAR_GROUP) == 0);
    CHECK(countOp(*c.code, protoST::Op::IVAR_GROUP_END) == 0);
}

TEST_CASE("IvarModifiers: a constant store ends a run of plain assignments", "[ivar_modifiers]") {
    // Two runs of two plain stores, around the constant: the constant joins
    // neither (it is a CAS, its own publication; spec §6.6).
    const auto c = compileSource(
        "Object subclass: #IvmConstB instanceVariableNames: 'history other' constantNames: 'number'.\n"
        "IvmConstB >> initialize\n"
        "  history := 1. other := 2. number := 3. history := 4. other := 5\n");
    REQUIRE(c.errors.empty());
    CHECK(countOp(*c.code, protoST::Op::STORE_CONST_INSTVAR) == 1);
    CHECK(countOp(*c.code, protoST::Op::IVAR_GROUP) == 2);
    CHECK(countOp(*c.code, protoST::Op::IVAR_GROUP_END) == 2);
    CHECK(countOp(*c.code, protoST::Op::STORE_INSTVAR) == 0);

    // The spec's shape: no group at all can hold `number`.
    const auto d = compileSource(
        "Object subclass: #IvmConstB2 instanceVariableNames: 'history' constantNames: 'number'.\n"
        "IvmConstB2 >> initialize\n"
        "  history := 1. number := 2. history := 3\n");
    REQUIRE(d.errors.empty());
    CHECK(countOp(*d.code, protoST::Op::IVAR_GROUP_END) == 0);
    CHECK(countOp(*d.code, protoST::Op::STORE_CONST_INSTVAR) == 1);
    CHECK(countOp(*d.code, protoST::Op::STORE_INSTVAR) == 2);
}

TEST_CASE("IvarModifiers: a constant in an inlined block of initialize, and its de-optimised copy",
          "[ivar_modifiers]") {
    const auto c = compileSource(
        "Object subclass: #IvmConstC instanceVariableNames: 'x' constantNames: 'number'.\n"
        "IvmConstC >> initialize\n"
        "  x > 0 ifTrue: [number := 1]\n");
    REQUIRE(c.errors.empty());
    std::vector<const protoST::BytecodeModule*> mods;
    modulesOf(*c.code, mods);
    int deoptCopies = 0, deoptStores = 0;
    for (const auto* mod : mods) {
        if (!mod->isDeoptCopy()) continue;
        ++deoptCopies;
        for (protoST::Op op : opsOf(*mod)) deoptStores += op == protoST::Op::STORE_CONST_INSTVAR ? 1 : 0;
    }
    CHECK(deoptCopies == 1);
    CHECK(deoptStores == 1);
    // The inlined body stores it too, and nothing stores it as a plain variable.
    CHECK(countOp(*c.code, protoST::Op::STORE_CONST_INSTVAR) == 2);
    CHECK(countOp(*c.code, protoST::Op::STORE_INSTVAR) == 0);
    // Only de-optimised copies carry the flag.
    CHECK_FALSE(c.code->isDeoptCopy());
}

TEST_CASE("IvarModifiers: nested inlined blocks of initialize may assign a constant", "[ivar_modifiers]") {
    // The de-optimised copy of the outer block inlines nothing, so the inner
    // block is a real block there; it stands for code the source inlined, so
    // the copy compiles to the constant store and reports nothing.
    const auto c = compileSource(
        "Object subclass: #IvmConstD instanceVariableNames: 'x y' constantNames: 'number'.\n"
        "IvmConstD >> initialize\n"
        "  x ifTrue: [y ifTrue: [number := 1]]\n");
    CHECK(c.errors.empty());
    CHECK(countOp(*c.code, protoST::Op::STORE_INSTVAR) == 0);
    // One store each: the inlined code, the copy of the inner block (made by
    // the inlined outer one), and the real inner block in the outer copy.
    CHECK(countOp(*c.code, protoST::Op::STORE_CONST_INSTVAR) == 3);
}

TEST_CASE("IvarModifiers: M1, M2 and M4 at compile time", "[ivar_modifiers]") {
    const char* decl =
        "Object subclass: #IvmRules instanceVariableNames: 'h' constantNames: 'number' "
        "atomicNames: 'balance'.\n";
    SECTION("a constant outside initialize") {
        const auto c = compileSource(std::string(decl) + "IvmRules >> setNumber: n\n  number := n\n");
        CHECK(hasErrorContaining(c, "cannot assign to the constant 'number' of IvmRules outside "
                                    "initialize (line 3)"));
    }
    SECTION("a constant in a class-side initialize") {
        const auto c = compileSource(std::string(decl)
            + "IvmRules class instanceVariableNames: 'number'.\n"
              "IvmRules class >> initialize\n  number := 1\n");
        CHECK(c.errors.empty());   // the class-side variable is another, plain one
    }
    SECTION("a constant inside a real block of initialize") {
        const auto c = compileSource(std::string(decl) + "IvmRules >> initialize\n  [number := 1] value\n");
        CHECK(hasErrorContaining(c, "cannot assign to the constant 'number' of IvmRules inside a "
                                    "block: the block may run after construction (line 3)"));
    }
    SECTION("a real block inside an inlined block of initialize") {
        // The de-optimised copies do not make a real closure acceptable:
        // the inlined code itself holds the closure.
        const auto c = compileSource(std::string(decl)
            + "IvmRules >> initialize\n  h ifTrue: [[number := 1] value]\n");
        CHECK(hasErrorContaining(c, "cannot assign to the constant 'number' of IvmRules inside a "
                                    "block: the block may run after construction (line 3)"));
    }
    SECTION("an atomic variable, in initialize too") {
        const auto c = compileSource(std::string(decl) + "IvmRules >> initialize\n  ^ balance := 0\n");
        CHECK(hasErrorContaining(c, "cannot assign to the atomic variable 'balance' of IvmRules: "
                                    "use update:with: or compareAndSet:from:to: (line 3)"));
    }
    SECTION("a plain variable is unaffected") {
        const auto c = compileSource(std::string(decl) + "IvmRules >> setH: x\n  h := x\n");
        CHECK(c.errors.empty());
    }
}

TEST_CASE("IvarModifiers: a late store keeps its value: DUP STORE_LATE_VAR POP", "[ivar_modifiers]") {
    // Pre-flight ruling C2: install time may rewrite the store into an
    // opcode that replaces the top of the stack, so every late store is
    // emitted with the DUP, and a discarded value is popped after it.
    const auto c = compileSource(
        "(Object yourself) subclass: #IvmLate instanceVariableNames: 'v'.\n"
        "IvmLate >> f\n"
        "  v := 1.\n"
        "  ^ 2\n");
    REQUIRE(c.errors.empty());
    std::vector<const protoST::BytecodeModule*> mods;
    modulesOf(*c.code, mods);
    bool found = false;
    for (const auto* mod : mods) {
        const auto ops = opsOf(*mod);
        for (std::size_t i = 0; i < ops.size(); ++i) {
            if (ops[i] != protoST::Op::STORE_LATE_VAR) continue;
            found = true;
            REQUIRE(i > 0);
            REQUIRE(i + 1 < ops.size());
            CHECK(ops[i - 1] == protoST::Op::DUP);
            CHECK(ops[i + 1] == protoST::Op::POP);
        }
    }
    CHECK(found);
}

namespace {

// The opcodes on each side of every `op` in the tree, as (before, after)
// pairs; an edge of a module answers EXTEND.
std::vector<std::pair<protoST::Op, protoST::Op>> neighboursOf(const protoST::BytecodeModule& root,
                                                              protoST::Op op) {
    std::vector<const protoST::BytecodeModule*> mods;
    modulesOf(root, mods);
    std::vector<std::pair<protoST::Op, protoST::Op>> out;
    for (const auto* mod : mods) {
        const auto ops = opsOf(*mod);
        for (std::size_t i = 0; i < ops.size(); ++i) {
            if (ops[i] != op) continue;
            out.emplace_back(i > 0 ? ops[i - 1] : protoST::Op::EXTEND,
                             i + 1 < ops.size() ? ops[i + 1] : protoST::Op::EXTEND);
        }
    }
    return out;
}

} // namespace

TEST_CASE("IvarModifiers: a write-once store is DUP STORE_ONCE_INSTVAR then RETURN or POP",
          "[ivar_modifiers]") {
    // Pre-flight rulings C2 and C6: STORE_ONCE_INSTVAR takes `x v -> w`, so
    // it always follows a DUP; its answer is returned by `^ key := x`, and
    // popped when the statement's value is discarded.
    const auto ret = compileSource(
        "Object subclass: #IvmOnceA instanceVariableNames: 'h' writeOnceNames: 'key'.\n"
        "IvmOnceA >> setKey: x\n"
        "  ^ key := x\n");
    REQUIRE(ret.errors.empty());
    CHECK(countOp(*ret.code, protoST::Op::STORE_INSTVAR) == 0);
    const auto r = neighboursOf(*ret.code, protoST::Op::STORE_ONCE_INSTVAR);
    REQUIRE(r.size() == 1);
    CHECK(r[0].first == protoST::Op::DUP);
    CHECK(r[0].second == protoST::Op::RETURN);

    const auto stmt = compileSource(
        "Object subclass: #IvmOnceB instanceVariableNames: 'h' writeOnceNames: 'key'.\n"
        "IvmOnceB >> setKey: x\n"
        "  h := 1.\n"
        "  key := x.\n"
        "  h := 2.\n"
        "  ^ 3\n");
    REQUIRE(stmt.errors.empty());
    const auto s = neighboursOf(*stmt.code, protoST::Op::STORE_ONCE_INSTVAR);
    REQUIRE(s.size() == 1);
    CHECK(s[0].first == protoST::Op::DUP);
    CHECK(s[0].second == protoST::Op::POP);
    // A write-once store never joins a run of assignments (spec §6.6).
    CHECK(countOp(*stmt.code, protoST::Op::IVAR_GROUP) == 0);

    // Inside a real block, and as the value of a temporary's assignment.
    const auto nested = compileSource(
        "Object subclass: #IvmOnceC instanceVariableNames: 'h' writeOnceNames: 'key'.\n"
        "IvmOnceC >> setKey: x\n"
        "  | t |\n"
        "  t := key := x.\n"
        "  ^ [key := t] value\n");
    REQUIRE(nested.errors.empty());
    const auto n = neighboursOf(*nested.code, protoST::Op::STORE_ONCE_INSTVAR);
    REQUIRE(n.size() == 2);
    for (const auto& p : n) CHECK(p.first == protoST::Op::DUP);
}

TEST_CASE("IvarModifiers: a constant read compiles to PUSH_CONST_INSTVAR", "[ivar_modifiers]") {
    // Spec §6.2: a compiled read of a constant, in a method or a block, is
    // the checked opcode; plain, write-once and atomic reads keep
    // PUSH_INSTVAR, and a class-side method's class-instance variables are
    // always plain (G1).
    const auto c = compileSource(
        "Object subclass: #IvmReadA instanceVariableNames: 'plain' constantNames: 'number'\n"
        "  writeOnceNames: 'once' atomicNames: 'count'.\n"
        "IvmReadA >> number\n"
        "  ^ number\n"
        "IvmReadA >> later\n"
        "  ^ [number + 1] value\n"
        "IvmReadA >> others\n"
        "  ^ {plain. once. count}\n");
    REQUIRE(c.errors.empty());
    CHECK(countOp(*c.code, protoST::Op::PUSH_CONST_INSTVAR) == 2);
    CHECK(countOp(*c.code, protoST::Op::PUSH_INSTVAR) == 3);

    const auto cls = compileSource(
        "Object subclass: #IvmReadB instanceVariableNames: 'x' constantNames: 'number'.\n"
        "IvmReadB class instanceVariableNames: 'number'.\n"
        "IvmReadB class >> number\n"
        "  ^ number\n");
    REQUIRE(cls.errors.empty());
    CHECK(countOp(*cls.code, protoST::Op::PUSH_CONST_INSTVAR) == 0);
    CHECK(countOp(*cls.code, protoST::Op::PUSH_OWN_INSTVAR) == 1);
}

// ---------------------------------------------------------------------------
// The generated constructor (explicit-state spec §6.2, plan decision D-d,
// pre-flight ruling C11; Task 6)
// ---------------------------------------------------------------------------

namespace {

// Each instruction of `m` as (opcode, operand), EXTEND prefixes folded in.
std::vector<std::pair<protoST::Op, unsigned int>> instructionsOf(const protoST::BytecodeModule& m) {
    std::vector<std::pair<protoST::Op, unsigned int>> out;
    const auto& b = m.bytes();
    for (std::size_t pc = 0; pc + 1 < b.size();) {
        auto op = static_cast<protoST::Op>(b[pc]);
        unsigned int arg = b[pc + 1];
        pc += protoST::kInstrSize;
        while (op == protoST::Op::EXTEND && pc + 1 < b.size()) {
            op = static_cast<protoST::Op>(b[pc]);
            arg = (arg << 8) | b[pc + 1];
            pc += protoST::kInstrSize;
        }
        out.emplace_back(op, arg);
    }
    return out;
}

} // namespace

TEST_CASE("IvarModifiers: the generated constructor is a class-side method over every constant",
          "[ivar_modifiers]") {
    const char* src =
        "Object subclass: #GcAccount instanceVariableNames: 'seen' constantNames: 'number owner'.\n"
        "GcAccount subclass: #GcSub instanceVariableNames: '' constantNames: 'extra'.\n"
        "GcAccount subclass: #GcSame instanceVariableNames: 'more'.\n"
        "Object subclass: #GcPlain instanceVariableNames: 'p'.\n"
        "0\n";
    protoST::Parser P(src);
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*mod);
    REQUIRE_FALSE(C.hasErrors());
    protoST::STRuntime rt;
    std::string error;
    try {
        rt.runTopLevel(*bc);
    } catch (const std::exception& e) {
        error = protoST::nativeErrorText(e);
    }
    REQUIRE(error.empty());
    proto::ProtoContext* ctx = rt.rootCtx();
    auto sym = [&](const char* s) { return proto::ProtoString::createSymbol(ctx, s); };
    auto global = [&](const char* name) { return rt.globals()->getAttribute(ctx, sym(name)); };
    auto text = [&](const proto::ProtoObject* v) {
        const proto::ProtoString* s = v && v != PROTO_NONE ? v->asString(ctx) : nullptr;
        return s ? s->toStdString(ctx) : std::string();
    };
    auto recorded = [&](const char* name) {
        return text(global(name)->getOwnAttributeDirect(ctx, sym("__constant_constructor__")));
    };
    CHECK(recorded("GcAccount") == "number:owner:");
    CHECK(recorded("GcSub") == "number:owner:extra:");
    // A subclass that adds no constant owns no copy: it inherits the
    // superclass's constructor and record (Task 6 ruling I2).
    CHECK(recorded("GcSame").empty());
    CHECK(text(global("GcSame")->getAttribute(ctx, sym("__constant_constructor__"))) == "number:owner:");
    CHECK(recorded("GcPlain").empty());

    const proto::ProtoObject* meta = global("GcAccount")->getOwnAttributeDirect(ctx, sym("__metaclass__"));
    REQUIRE(meta != nullptr);
    const proto::ProtoObject* method = meta->getOwnAttributeDirect(ctx, sym("number:owner:"));
    REQUIRE(method != nullptr);
    const protoST::BytecodeModule* code =
        protoST::blockCodeOf(ctx, method->getOwnAttributeDirect(ctx, sym("__bc_ptr__")));
    REQUIRE(code != nullptr);
    CHECK(code->argCount() == 3);
    // The body (D-d as amended by the Task 6 ruling on concern 1; C11: local
    // 0 is self, the arguments are locals 1..n), pinned by opcode and operand.
    using protoST::Op;
    const auto ins = instructionsOf(*code);
    std::vector<Op> ops;
    for (const auto& i : ins) ops.push_back(i.first);
    CHECK(ops == std::vector<Op>{Op::PUSH_SELF, Op::PUSH_SELF, Op::PUSH_LOCAL, Op::PUSH_LOCAL,
                                 Op::MAKE_ARRAY, Op::PUSH_CONST, Op::SEND_KEYWORD, Op::DUP,
                                 Op::SEND_UNARY, Op::POP, Op::PUSH_CONST, Op::SEND_KEYWORD,
                                 Op::RETURN});
    REQUIRE(ins.size() == 13);
    CHECK(ins[2].second == 1);
    CHECK(ins[3].second == 2);
    CHECK(ins[4].second == 2);
    CHECK(code->constSymbol(ins[5].second) == "number:owner:");
    CHECK(code->constSymbol(ins[6].second) == "__constructConstants:selector:");
    CHECK(code->constSymbol(ins[8].second) == "initialize");
    CHECK(code->constSymbol(ins[10].second) == "number:owner:");
    CHECK(code->constSymbol(ins[11].second) == "__checkConstantsOf:after:");
}

// The constant store on an immutable receiver (Task 3/4 ruling, Task 8). No
// Smalltalk route reaches it: a constant is assigned only in initialize and
// its inlined blocks, and a de-optimised copy of those blocks takes no
// argument, so none can run as a method of an immutable receiver. So the
// compiled initialize of a class is taken from the class here and installed
// as a BlockClosure method; sent to a block, its STORE_CONST_INSTVAR meets an
// immutable self and signals ModificationForbidden in place, with the text
// pinned below, catchable by class. The block is left unchanged.
TEST_CASE("IvarModifiers: a constant store on an immutable receiver signals ModificationForbidden",
          "[ivar_modifiers]") {
    const char* declare =
        "Object subclass: #ImAccount instanceVariableNames: '' constantNames: 'number'.\n"
        "ImAccount >> initialize\n"
        "  number := 1\n"
        "\n"
        "0\n";
    protoST::Parser P(declare);
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*mod);
    REQUIRE_FALSE(C.hasErrors());
    protoST::STRuntime rt;
    std::string error;
    try {
        rt.runTopLevel(*bc);
    } catch (const std::exception& e) {
        error = protoST::nativeErrorText(e);
    }
    REQUIRE(error.empty());
    proto::ProtoContext* ctx = rt.rootCtx();
    auto sym = [&](const char* s) { return proto::ProtoString::createSymbol(ctx, s); };
    const proto::ProtoObject* cls = rt.globals()->getAttribute(ctx, sym("ImAccount"));
    REQUIRE(cls != nullptr);
    const proto::ProtoObject* init = cls->getOwnAttributeDirect(ctx, sym("initialize"));
    REQUIRE(init != nullptr);
    REQUIRE(init != PROTO_NONE);
    rt.globals()->setAttribute(ctx, sym("ImInitialize"), init);

    const char* run =
        "| target |\n"
        "BlockClosure __installMethod: ImInitialize as: #poke.\n"
        "target := [:a | a].\n"
        "([target poke. 'no error'] on: ModificationForbidden do: [:e | e messageText])\n"
        "  , ' / ' , (target instVarNamed: 'number') printString\n";
    protoST::Parser P2(run);
    auto mod2 = P2.parseModule();
    REQUIRE(P2.errors().empty());
    protoST::Compiler C2;
    auto bc2 = C2.compileModule(*mod2);
    REQUIRE_FALSE(C2.hasErrors());
    const proto::ProtoObject* answer = nullptr;
    try {
        answer = rt.runTopLevel(*bc2);
    } catch (const std::exception& e) {
        error = protoST::nativeErrorText(e);
    }
    REQUIRE(error.empty());
    const proto::ProtoString* s = answer && answer != PROTO_NONE ? answer->asString(ctx) : nullptr;
    REQUIRE(s != nullptr);
    CHECK(s->toStdString(ctx)
          == "cannot assign to the constant 'number' of a BlockClosure: the receiver is "
             "immutable; nothing was written / nil");
}

TEST_CASE("IvarModifiers: the mutability probe answers true only for a mutable object and leaves "
          "nothing behind",
          "[ivar_modifiers]") {
    // The state primitives tell an immutable receiver from a lost race with
    // protoCore's setAttributeIfEqual(probe, nullptr, nullptr) (Task 7
    // review 1, I2). Its nullptr new value is not documented by protoCore
    // 3.6.1, so this case pins the behaviour the primitives rely on.
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();
    const proto::ProtoString* key = proto::ProtoString::createSymbol(ctx, "_iv_v");
    const proto::ProtoObject* mut = ctx->newObject(true);
    const proto::ProtoObject* imm = ctx->newObject(false);
    mut->setAttribute(ctx, key, ctx->fromInteger(5));
    for (int i = 0; i < 3; ++i) {
        CHECK(protoST::receiverIsWritable(rt, ctx, mut));
        CHECK_FALSE(protoST::receiverIsWritable(rt, ctx, imm));
        CHECK_FALSE(protoST::receiverIsWritable(rt, ctx, ctx->fromInteger(3)));
    }
    const proto::ProtoString* probe = rt.bootstrap().sym.mutabilityProbe;
    REQUIRE(probe != nullptr);
    CHECK(mut->getOwnAttributeDirect(ctx, probe) == nullptr);
    CHECK(mut->hasOwnAttribute(ctx, probe) != PROTO_TRUE);
    CHECK(mut->getOwnAttributeDirect(ctx, key)->asLong(ctx) == 5);
}

TEST_CASE("IvarModifiers: M14 is signalled through the UnsetConstantError captured at bootstrap, "
          "not through the global name",
          "[ivar_modifiers]") {
    // Explicit-state spec §6.9 (Task 11 fix round 1). Every language route
    // that would rebind the reserved global is refused; this case rebinds it
    // from C++, past all of them, and checks that a read of an unset
    // constant still signals the kernel class, with its accessors.
    protoST::STRuntime rt;
    proto::ProtoContext* ctx = rt.rootCtx();
    auto sym = [&](const char* s) { return proto::ProtoString::createSymbol(ctx, s); };
    const proto::ProtoObject* kernel = rt.globals()->getAttribute(ctx, sym("UnsetConstantError"));
    REQUIRE(kernel != nullptr);
    REQUIRE(kernel != PROTO_NONE);
    CHECK(rt.bootstrap().unsetConstantErrorClass == kernel);
    rt.globals()->setAttribute(ctx, sym("KernelUnsetConstantError"), kernel);
    rt.globals()->setAttribute(ctx, sym("UnsetConstantError"), ctx->fromInteger(42));

    const char* run =
        "Object subclass: #Acct instanceVariableNames: '' constantNames: 'owner'.\n"
        "Acct >> owner\n"
        "  ^ owner\n"
        "\n"
        "[[Acct basicNew owner. 'no error']\n"
        "   on: KernelUnsetConstantError do: [:e | e variableName]]\n"
        "  on: Error do: [:e | 'a plain Error']\n";
    protoST::Parser P(run);
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*mod);
    REQUIRE_FALSE(C.hasErrors());
    const proto::ProtoObject* answer = nullptr;
    std::string error;
    try {
        answer = rt.runTopLevel(*bc);
    } catch (const std::exception& e) {
        error = protoST::nativeErrorText(e);
    }
    REQUIRE(error.empty());
    const proto::ProtoString* s = answer && answer != PROTO_NONE ? answer->asString(ctx) : nullptr;
    REQUIRE(s != nullptr);
    CHECK(s->toStdString(ctx) == "owner");
}
