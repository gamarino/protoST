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
// Task 3 implements the assignment rules and removes the [!mayfail] tag.
TEST_CASE("IvarModifiers: a later REPL method that assigns an atomic variable reports M4",
          "[ivar_modifiers][!mayfail]") {
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
