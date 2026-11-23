// Instance-variable modifiers (explicit-state spec §6.1): the
// constantNames:, writeOnceNames: and atomicNames: clauses of a class
// declaration, the records the compiler keeps for them, and the helpers of
// src/runtime/IvarModifiers.h.

#include <catch2/catch_all.hpp>

#include <exception>
#include <string>
#include <vector>

#include "frontend/Compiler.h"
#include "protoST/STRuntime.h"
#include "runtime/NativeError.h"
#include "protoCore.h"
#include "frontend/Parser.h"
#include "runtime/IvarModifiers.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

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
    CHECK(countOp(*c.code, protoST::Op::STORE_CONST_INSTVAR) >= 3);
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
