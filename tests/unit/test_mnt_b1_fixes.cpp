// MNT-b1 — regression pins for the bug-fix slice covering D1, D9, D13, D15,
// D16 and D18 (see docs/STATUS.md).
//
//  D1   negative numeric literals lex in operand/primary position
//  D9   the control-flow / nil-test selector set on Boolean, Object and Block
//  D13  the CLI usage text no longer advertises an unimplemented `compile`
//  D15  `classVariableNames:` emits a diagnostic rather than a silent no-op
//  D16  nested literal arrays parse
//  D18  identity (`==`/`~~`) and universal equality (`=`/`~=`)
//
// These pin the source -> parse -> compile -> run behaviour so a regression is
// caught here, close to the cause, even if the conformance runner is not run.

#include <catch2/catch_all.hpp>

#include <stdexcept>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

namespace {

// Compile `src` and run it at top level, returning the module's result.
const proto::ProtoObject* runSrc(protoST::STRuntime& rt, const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return rt.runTopLevel(*bc);
}

// Parse `src` and report whether the parser produced any diagnostics.
bool parsesCleanly(const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    return P.errors().empty();
}

} // namespace

// --- D1: negative numeric literals -----------------------------------------

TEST_CASE("MNT-b1 D1: a leading -digit lexes as a negative literal",
          "[mnt-b1][D1]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    {
        auto* r = runSrc(rt, "-5.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == -5);
    }
    {
        // A negative literal as a message argument.
        auto* r = runSrc(rt, "3 + -2.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 1);
    }
}

TEST_CASE("MNT-b1 D1: a negative float literal parses",
          "[mnt-b1][D1]") {
    REQUIRE(parsesCleanly("-3.14."));
}

TEST_CASE("MNT-b1 D1: a `-` after an operand stays binary minus",
          "[mnt-b1][D1]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    {
        // `3 - 5` is subtraction, not the number 3 followed by the literal -5.
        auto* r = runSrc(rt, "3 - 5.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == -2);
    }
    {
        // `-` glued to a closing paren is binary minus.
        auto* r = runSrc(rt, "(1 + 1) - 5.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == -3);
    }
    {
        // `3 - -2` — binary minus then a negative literal.
        auto* r = runSrc(rt, "3 - -2.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 5);
    }
}

TEST_CASE("MNT-b1 D1: negative literals inside a literal array",
          "[mnt-b1][D1][D16]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    auto* r = runSrc(rt, "#(-1 -2 -3) size.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(ctx) == 3);
}

// --- D9: control-flow / nil selectors --------------------------------------

TEST_CASE("MNT-b1 D9: ifTrue:ifFalse: / ifFalse:ifTrue: on Boolean",
          "[mnt-b1][D9]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    REQUIRE(runSrc(rt, "true ifTrue: ['a'] ifFalse: ['b'].") != nullptr);
    REQUIRE(runSrc(rt, "(3 > 0) ifTrue: [1] ifFalse: [2].")->asLong(ctx) == 1);
    REQUIRE(runSrc(rt, "(3 < 0) ifTrue: [1] ifFalse: [2].")->asLong(ctx) == 2);
    REQUIRE(runSrc(rt, "true ifFalse: [1] ifTrue: [2].")->asLong(ctx) == 2);
}

TEST_CASE("MNT-b1 D9: and: / or: / not on Boolean", "[mnt-b1][D9]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "true and: [true].")   == PROTO_TRUE);
    REQUIRE(runSrc(rt, "true and: [false].")  == PROTO_FALSE);
    REQUIRE(runSrc(rt, "false and: [true].")  == PROTO_FALSE);
    REQUIRE(runSrc(rt, "false or: [false].")  == PROTO_FALSE);
    REQUIRE(runSrc(rt, "false or: [true].")   == PROTO_TRUE);
    REQUIRE(runSrc(rt, "true or: [false].")   == PROTO_TRUE);
    REQUIRE(runSrc(rt, "true not.")           == PROTO_FALSE);
    REQUIRE(runSrc(rt, "false not.")          == PROTO_TRUE);
}

TEST_CASE("MNT-b1 D9: eager & / | on Boolean", "[mnt-b1][D9]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "true & true.")   == PROTO_TRUE);
    REQUIRE(runSrc(rt, "true & false.")  == PROTO_FALSE);
    REQUIRE(runSrc(rt, "false | true.")  == PROTO_TRUE);
    REQUIRE(runSrc(rt, "false | false.") == PROTO_FALSE);
}

TEST_CASE("MNT-b1 D9: nil-test protocol on Object", "[mnt-b1][D9]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    REQUIRE(runSrc(rt, "nil isNil.")  == PROTO_TRUE);
    REQUIRE(runSrc(rt, "3 isNil.")    == PROTO_FALSE);
    REQUIRE(runSrc(rt, "nil notNil.") == PROTO_FALSE);
    REQUIRE(runSrc(rt, "3 notNil.")   == PROTO_TRUE);
    REQUIRE(runSrc(rt, "nil ifNil: [42].")->asLong(ctx) == 42);
    REQUIRE(runSrc(rt, "7 ifNil: [42].")->asLong(ctx) == 7);
    REQUIRE(runSrc(rt, "3 ifNotNil: [:x | x + 1].")->asLong(ctx) == 4);
    REQUIRE(runSrc(rt, "nil ifNotNil: [:x | x]." ) == PROTO_NONE);
    REQUIRE(runSrc(rt, "nil ifNil: [1] ifNotNil: [:x | 2].")->asLong(ctx) == 1);
    REQUIRE(runSrc(rt, "9 ifNil: [1] ifNotNil: [:x | x].")->asLong(ctx) == 9);
}

TEST_CASE("MNT-b1 D9: whileFalse: on Block", "[mnt-b1][D9]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    auto* r = runSrc(rt, "n := 0. [n >= 3] whileFalse: [n := n + 1]. n.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(ctx) == 3);
}

// --- D15: classVariableNames: diagnostic -----------------------------------

TEST_CASE("MNT-b1 D15: a non-empty classVariableNames: clause is diagnosed",
          "[mnt-b1][D15]") {
    protoST::Parser P("Object subclass: #Counter "
                      "instanceVariableNames: 'value' "
                      "classVariableNames: 'Total'.");
    auto ast = P.parseModule();
    // The clause must no longer be silently discarded.
    REQUIRE_FALSE(P.errors().empty());
}

TEST_CASE("MNT-b1 D15: an empty classVariableNames: clause stays silent",
          "[mnt-b1][D15]") {
    // `classVariableNames: ''` requests nothing — a documented no-op.
    REQUIRE(parsesCleanly("Object subclass: #Foo "
                          "instanceVariableNames: 'value' "
                          "classVariableNames: ''."));
}

// --- D16: nested literal arrays --------------------------------------------

TEST_CASE("MNT-b1 D16: a nested #( ) inside #( ) parses",
          "[mnt-b1][D16]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    REQUIRE(runSrc(rt, "#(1 #(2 3) 4) size.")->asLong(ctx) == 3);
    REQUIRE(runSrc(rt, "#(#(1 2) #(3 4)) size.")->asLong(ctx) == 2);
}

TEST_CASE("MNT-b1 D16: a bare ( ) group inside #( ) is a nested literal array",
          "[mnt-b1][D16]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    REQUIRE(runSrc(rt, "#(1 (2 3) 4) size.")->asLong(ctx) == 3);
}

// --- D18: identity and universal equality ----------------------------------

TEST_CASE("MNT-b1 D18: == / ~~ are bound on every object",
          "[mnt-b1][D18]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "3 == 3.")       == PROTO_TRUE);
    REQUIRE(runSrc(rt, "3 == 4.")       == PROTO_FALSE);
    REQUIRE(runSrc(rt, "3 ~~ 4.")       == PROTO_TRUE);
    REQUIRE(runSrc(rt, "3 ~~ 3.")       == PROTO_FALSE);
    // Interned symbols are identical.
    REQUIRE(runSrc(rt, "#foo == #foo.") == PROTO_TRUE);
    REQUIRE(runSrc(rt, "#foo ~~ #bar.") == PROTO_TRUE);
}

TEST_CASE("MNT-b1 D18: == is bound on user-defined objects",
          "[mnt-b1][D18]") {
    protoST::STRuntime rt;
    // A fresh object is identical to itself but not to a sibling.
    REQUIRE(runSrc(rt, "Object subclass: #Widget. "
                       "w := Widget new. w == w.") == PROTO_TRUE);
    REQUIRE(runSrc(rt, "Object subclass: #Gadget. "
                       "(Gadget new) == (Gadget new).") == PROTO_FALSE);
}

TEST_CASE("MNT-b1 D18: value equality holds on the primitive types",
          "[mnt-b1][D18]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "3 = 3.")          == PROTO_TRUE);
    REQUIRE(runSrc(rt, "3 ~= 4.")         == PROTO_TRUE);
    REQUIRE(runSrc(rt, "'a' = 'a'.")      == PROTO_TRUE);
    REQUIRE(runSrc(rt, "'a' ~= 'b'.")     == PROTO_TRUE);
    REQUIRE(runSrc(rt, "true = true.")    == PROTO_TRUE);
    REQUIRE(runSrc(rt, "true = false.")   == PROTO_FALSE);
    REQUIRE(runSrc(rt, "nil isNil.")      == PROTO_TRUE);
}
