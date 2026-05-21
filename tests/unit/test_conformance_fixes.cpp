// Track 6, slice 3 — regression pins for the conformance-suite discrepancies.
//
// The conformance suite (tests/conformance/) proves spec-conformance against
// docs/LANGUAGE.md as a black box. These unit tests pin the *behavioural*
// fixes (#1-#4 of the slice) at the source -> parse -> compile -> run level so
// a regression is caught here, close to the cause, even if the conformance
// runner is not exercised.
//
//  #1  chained assignment `a := b := 0` (§3.6 — assignment is an expression)
//  #2  `isEven` / `isOdd` bound on SmallInteger (§7, §9.2)
//  #3  `Future new` yields a usable, manually-resolvable promise (§10.3)
//  #4  `wait` on a rejected actor Future re-raises the rejection (§10.3/§10.7)

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

} // namespace

// --- #1: chained assignment ------------------------------------------------

TEST_CASE("conformance #1: chained assignment binds both names",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    {
        // The outer assignment's value is the inner assignment's value.
        auto* r = runSrc(rt, "a := b := 5. a.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 5);
    }
    {
        // The inner name is bound too.
        auto* r = runSrc(rt, "a := b := 5. b.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 5);
    }
}

TEST_CASE("conformance #1: an assignment is an expression yielding its value",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    // An assignment used as the argument of a message send.
    auto* r = runSrc(rt, "x := 0. (x := 7) + 1.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(ctx) == 8);
}

TEST_CASE("conformance #1: chained assignment parses without error",
          "[conformance][track6]") {
    protoST::Parser P("a := b := 0. a.");
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
}

// --- #2: isEven / isOdd ----------------------------------------------------

TEST_CASE("conformance #2: isEven / isOdd are bound on SmallInteger",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    REQUIRE(runSrc(rt, "4 isEven.") == PROTO_TRUE);
    REQUIRE(runSrc(rt, "4 isOdd.")  == PROTO_FALSE);
    REQUIRE(runSrc(rt, "7 isEven.") == PROTO_FALSE);
    REQUIRE(runSrc(rt, "7 isOdd.")  == PROTO_TRUE);
    REQUIRE(runSrc(rt, "0 isEven.") == PROTO_TRUE);
}

// --- #3: Future new is a usable promise ------------------------------------

TEST_CASE("conformance #3: Future new + resolve: + wait yields the value",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    auto* r = runSrc(rt, "f := Future new. f resolve: 7. f wait.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(ctx) == 7);
}

TEST_CASE("conformance #3: a directly-constructed Future starts pending",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    // resolve: is idempotent — the second resolve: is a no-op, so wait still
    // observes the first value. This only holds if `Future new` produced a
    // proper pending future with the full state machinery.
    auto* r = runSrc(rt, "f := Future new. f resolve: 11. f resolve: 22. f wait.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(ctx) == 11);
}

// --- #4: wait on a rejected Future re-raises -------------------------------

TEST_CASE("conformance #4: wait on a directly-rejected Future re-raises",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    // A rejected Future's wait must surface the rejection as an error rather
    // than silently returning the cause.
    REQUIRE_THROWS_AS(
        runSrc(rt, "f := Future new. f rejectWith: 'boom'. f wait."),
        std::exception);
}

TEST_CASE("conformance #4: wait re-raises a rejection from an actor method",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    // An exception unhandled inside an actor method rejects that message's
    // Future (§10.7); waiting on it re-raises (§10.3). The `boom` method ends
    // with an explicit `^ self` so — per §3.4 — the trailing top-level lines
    // are NOT swallowed into the method body.
    const char* src =
        "Object subclass: #Faulty. "
        "Faulty >> boom "
        "  Error signal: 'actor failure'. "
        "  ^ self. "
        "a := Faulty new asActor. "
        "(a boom) wait.";
    REQUIRE_THROWS_AS(runSrc(rt, src), std::exception);
}

TEST_CASE("conformance #4: a wait on a resolved actor Future returns the value",
          "[conformance][track6]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    // The positive counterpart: a non-faulting actor method resolves its
    // Future and wait returns the value.
    const char* src =
        "Object subclass: #Ok. "
        "Ok >> answer "
        "  ^ 42. "
        "a := Ok new asActor. "
        "(a answer) wait.";
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(ctx) == 42);
}
