// Track 1, slice 2 (EXC-a + EXC-b) — the exception protocol core.
//
// Exercises the class hierarchy (`Exception`/`Error`/`Warning`), `signal` /
// `signal:`, the protected-block primitives `on:do:` / `on:do:on:do:`, and
// the handler actions `return:`, `resume:`, `retry`, `pass` / `outer`, plus
// handler fall-through and the refined default action. `ensure:`/
// `ifCurtailed:` is EXC-c; native exception translation is EXC-d — neither is
// covered here.
//
// See docs/superpowers/specs/2026-05-20-exceptions.md.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
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

TEST_CASE("EXC-a: on:do: catches a signalled Error and yields the handler's "
          "value",
          "[exceptions][track1]") {
    // `[ Error signal: 'boom' ] on: Error do: [ :e | e messageText ]` — the
    // handler returns the exception's messageText, which the `on:do:` yields.
    const char* src =
        "Object subclass: #Risky. "
        "Risky >> run "
        "  ^ [ Error signal: 'boom' ] on: Error do: [ :e | e messageText ]. "
        "r := Risky newChild. "
        "r run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "boom");
}

TEST_CASE("EXC-a: handler fall-through yields the handler block's last value; "
          "code after signal is abandoned",
          "[exceptions][track1]") {
    // `[ Error signal: 'x'. 99 ] on: Error do: [ :e | 42 ]` — the handler
    // falls off its end with 42; the `on:do:` yields 42. The `99` after the
    // signal in the protected block never runs (the signal unwinds away).
    const char* src =
        "Object subclass: #Fall. "
        "Fall >> run "
        "  ^ [ Error signal: 'x'. 99 ] on: Error do: [ :e | 42 ]. "
        "f := Fall newChild. "
        "f run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("EXC-a: return: from a handler makes the on:do: yield the value",
          "[exceptions][track1]") {
    // `e return: 7` is the explicit form of handler fall-through: the owning
    // `on:do:` yields 7.
    const char* src =
        "Object subclass: #Ret. "
        "Ret >> run "
        "  ^ [ Error signal: 'x' ] on: Error do: [ :e | e return: 7 ]. "
        "r := Ret newChild. "
        "r run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("EXC-a: a protected block that completes normally yields its own "
          "value; the handler never runs",
          "[exceptions][track1]") {
    // No signal — the protected block produces 5, the `on:do:` yields 5, and
    // the handler block (which would yield 42) is never entered.
    const char* src =
        "Object subclass: #Calm. "
        "Calm >> run "
        "  ^ [ 2 + 3 ] on: Error do: [ :e | 42 ]. "
        "c := Calm newChild. "
        "c run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 5);
}

TEST_CASE("EXC-a: a user subclass of Error is caught by on: Error do:",
          "[exceptions][track1]") {
    // `Error subclass: #AppError` — an `AppError` instance IS an `Error`
    // (prototype-chain descendant), so `on: Error do:` matches it.
    const char* src =
        "Error subclass: #AppError. "
        "Object subclass: #Sub. "
        "Sub >> run "
        "  ^ [ AppError signal: 'app' ] on: Error do: [ :e | e messageText ]. "
        "s := Sub newChild. "
        "s run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "app");
}

TEST_CASE("EXC-a: a user subclass of Error is NOT caught by on: Warning do: — "
          "the unmatched exception aborts",
          "[exceptions][track1]") {
    // `AppErr2` is an `Error`, not a `Warning`. `on: Warning do:` does not
    // match it, so with no other handler the exception's default action
    // aborts the activation — surfaced as a runtime error.
    const char* src =
        "Error subclass: #AppErr2. "
        "Object subclass: #NoMatch. "
        "NoMatch >> run "
        "  ^ [ AppErr2 signal: 'app2' ] on: Warning do: [ :e | 1 ]. "
        "n := NoMatch newChild. "
        "n run.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(runSrc(rt, src),
                        Catch::Matchers::ContainsSubstring("app2"));
}

TEST_CASE("EXC-a: nested on:do: — the inner handler catches a matching "
          "exception",
          "[exceptions][track1]") {
    // The inner `on: Error do:` matches the signalled Error and handles it;
    // the outer handler never runs.
    const char* src =
        "Object subclass: #NestIn. "
        "NestIn >> run "
        "  ^ [ [ Error signal: 'x' ] on: Error do: [ :e | 11 ] ] "
        "      on: Error do: [ :e | 22 ]. "
        "n := NestIn newChild. "
        "n run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 11);
}

TEST_CASE("EXC-a: nested on:do: — an exception not matching the inner guard "
          "is caught by the outer handler",
          "[exceptions][track1]") {
    // The inner guard is `Warning`; the signalled `Error` does not match it,
    // so it propagates to the outer `on: Error do:`, which yields 88.
    const char* src =
        "Object subclass: #NestOut. "
        "NestOut >> run "
        "  ^ [ [ Error signal: 'x' ] on: Warning do: [ :e | 1 ] ] "
        "      on: Error do: [ :e | 88 ]. "
        "n := NestOut newChild. "
        "n run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 88);
}

TEST_CASE("EXC-a: a signal inside a handler is caught by an OUTER handler, "
          "not the handler's own on:do:",
          "[exceptions][track1]") {
    // The inner `on: Error do:` handler itself signals a second Error. Its
    // own entry (and inner ones) are disabled while it runs, so the second
    // signal escapes to the OUTER `on: Error do:`, which yields its message.
    const char* src =
        "Object subclass: #InHandler. "
        "InHandler >> run "
        "  ^ [ [ Error signal: 'first' ] "
        "        on: Error do: [ :e | Error signal: 'second' ] ] "
        "      on: Error do: [ :e | e messageText ]. "
        "i := InHandler newChild. "
        "i run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "second");
}

TEST_CASE("EXC-a: signalling an exception INSTANCE (not the class) runs the "
          "handler",
          "[exceptions][track1]") {
    // `Error new` builds an instance; `anInstance signal` goes through the
    // same handler-search path as the class-side form.
    const char* src =
        "Object subclass: #Inst. "
        "Inst >> run "
        "  ^ [ (Error new) signal ] on: Error do: [ :e | 55 ]. "
        "i := Inst newChild. "
        "i run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 55);
}

TEST_CASE("EXC-a: messageText: sets the exception's message; messageText "
          "reads it back",
          "[exceptions][track1]") {
    // The accessor pair on Exception. An instance built with `new`, given a
    // message via `messageText:`, then read back via `messageText`.
    const char* src =
        "Object subclass: #Accessor. "
        "Accessor >> run "
        "  | e | "
        "  e := Error new. "
        "  e messageText: 'hello'. "
        "  ^ e messageText. "
        "a := Accessor newChild. "
        "a run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "hello");
}

TEST_CASE("EXC-a: an unhandled Error signal surfaces as a runtime error",
          "[exceptions][track1]") {
    // No `on:do:` anywhere — the EXC-a default action aborts the top-level
    // activation with the exception's message.
    const char* src =
        "Error signal: 'unhandled'.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(runSrc(rt, src),
                        Catch::Matchers::ContainsSubstring("unhandled"));
}

TEST_CASE("EXC-a: an unhandled exception inside an actor handler rejects the "
          "message Future",
          "[exceptions][track1][actors]") {
    // The actor handler `boom` signals an Error with no `on:do:`. The default
    // action aborts the handler; drainOne rejects the message Future, which
    // the main thread's `wait` re-raises.
    const char* src =
        "Object subclass: #Boomer. "
        "Boomer >> boom "
        "  Error signal: 'actor-fault'. "
        "  ^ 0. "
        "ba := Boomer newChild asActor. "
        "f := ba boom. "
        "f wait.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(runSrc(rt, src),
                        Catch::Matchers::ContainsSubstring("actor-fault"));
}

TEST_CASE("EXC-a: on:do: inside an actor handler catches the exception and "
          "resolves the Future with the handler's value",
          "[exceptions][track1][actors]") {
    // The actor handler protects a signalling block with `on: Error do:`. The
    // handler runs in the actor's engine; the message Future resolves with
    // the handler's value.
    const char* src =
        "Object subclass: #SafeActor. "
        "SafeActor >> run "
        "  ^ [ Error signal: 'x' ] on: Error do: [ :e | 77 ]. "
        "sa := SafeActor newChild asActor. "
        "f := sa run. "
        "f wait.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 77);
}

TEST_CASE("EXC-a: Exception, Error and Warning resolve as globals and the "
          "hierarchy is correct",
          "[exceptions][track1]") {
    // `Warning subclass: ...` works (Warning is an ordinary prototype) and a
    // Warning subclass instance is caught by `on: Exception do:` (Exception
    // is the root of the hierarchy).
    const char* src =
        "Warning subclass: #LowFuel. "
        "Object subclass: #HierCheck. "
        "HierCheck >> run "
        "  ^ [ LowFuel signal: 'fuel' ] on: Exception do: [ :e | e messageText ]. "
        "h := HierCheck newChild. "
        "h run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "fuel");
}

// ===========================================================================
// EXC-b: resume:, retry, pass / outer, on:do:on:do:, refined default action.
// ===========================================================================

TEST_CASE("EXC-b: resume: makes the signal yield the resumed value and the "
          "protected block continues",
          "[exceptions][track1]") {
    // The handler does `e resume: 99`; the signal in the protected block
    // returns 99, is stored in `r`, and the post-signal expression `r` runs.
    // The protected block thus yields 99 — its stack was never unwound.
    const char* src =
        "Object subclass: #Res. "
        "Res >> run "
        "  ^ [ | r | r := Warning signal: 'w'. r ] "
        "      on: Warning do: [ :e | e resume: 99 ]. "
        "x := Res newChild. "
        "x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("EXC-b: resume with no argument resumes the signal with nil",
          "[exceptions][track1]") {
    // `e resume` == `e resume: nil`; the protected block continues and yields
    // a literal so the test has a definite value to assert.
    const char* src =
        "Object subclass: #ResNil. "
        "ResNil >> run "
        "  ^ [ Warning signal: 'w'. 7 ] on: Warning do: [ :e | e resume ]. "
        "x := ResNil newChild. "
        "x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("EXC-b: resume: on a non-resumable Error is itself an error",
          "[exceptions][track1]") {
    // `Error` is non-resumable; `e resume: 1` in its handler must fail.
    const char* src =
        "Object subclass: #BadResume. "
        "BadResume >> run "
        "  ^ [ Error signal: 'x' ] on: Error do: [ :e | e resume: 1 ]. "
        "x := BadResume newChild. "
        "x run.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(runSrc(rt, src),
                        Catch::Matchers::ContainsSubstring("resume"));
}

TEST_CASE("EXC-b: retry re-evaluates the protected block from scratch",
          "[exceptions][track1]") {
    // The first attempt: the protected block sees the gate state "pending",
    // flips it to "done", and signals an Error. The handler `retry`s. The
    // second attempt sees "done", skips the signal, and yields 42 — proving
    // the protected block re-ran. The one-shot gate guarantees termination
    // (a never-flipped flag would loop forever).
    //
    // The gate lives as an attribute on the globally-named subclass `RetryErr`
    // because, in the current build, block closures cannot capture method
    // locals or `self` — a globally-resolvable prototype is the only mutable
    // state a protected block can reach. (When block capture lands this test
    // can use an ordinary instance-variable counter.)
    const char* src =
        "Error subclass: #RetryErr. "
        "RetryErr messageText: 'pending'. "
        "Object subclass: #Retrier. "
        "Retrier >> run "
        "  ^ [ (RetryErr messageText = 'pending') "
        "        ifTrue: [ RetryErr messageText: 'done'. Error signal: 'first' ]. "
        "      42 ] "
        "      on: Error do: [ :e | e retry ]. "
        "x := Retrier newChild. "
        "x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("EXC-b: pass hands the exception to an outer handler",
          "[exceptions][track1]") {
    // The inner `on: Error do:` handler does `e pass`; the signal search
    // resumes outward and the outer `on: Error do:` handler runs, yielding 88.
    const char* src =
        "Object subclass: #Passer. "
        "Passer >> run "
        "  ^ [ [ Error signal: 'x' ] on: Error do: [ :e | e pass ] ] "
        "      on: Error do: [ :e | 88 ]. "
        "x := Passer newChild. "
        "x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 88);
}

TEST_CASE("EXC-b: outer is an alias of pass",
          "[exceptions][track1]") {
    // `e outer` behaves as `e pass` for this MVP — the outer handler runs.
    const char* src =
        "Object subclass: #Outerer. "
        "Outerer >> run "
        "  ^ [ [ Error signal: 'x' ] on: Error do: [ :e | e outer ] ] "
        "      on: Error do: [ :e | 71 ]. "
        "x := Outerer newChild. "
        "x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 71);
}

TEST_CASE("EXC-b: pass with no outer handler runs the default action",
          "[exceptions][track1]") {
    // A single handler does `e pass`; with no outer handler the Error's
    // default action aborts the activation with its message.
    const char* src =
        "Object subclass: #PassNone. "
        "PassNone >> run "
        "  ^ [ Error signal: 'fellthrough' ] on: Error do: [ :e | e pass ]. "
        "x := PassNone newChild. "
        "x run.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(runSrc(rt, src),
                        Catch::Matchers::ContainsSubstring("fellthrough"));
}

TEST_CASE("EXC-b: on:do:on:do: — the matching guard's handler runs",
          "[exceptions][track1]") {
    // Two guards on one protected block. A signalled Error matches the Error
    // guard and yields 1; a signalled Warning matches the Warning guard and
    // yields 2.
    const char* errSrc =
        "Object subclass: #TwoGuardE. "
        "TwoGuardE >> run "
        "  ^ [ Error signal: 'e' ] "
        "      on: Error   do: [ :e | 1 ] "
        "      on: Warning do: [ :e | 2 ]. "
        "x := TwoGuardE newChild. "
        "x run.";
    const char* warnSrc =
        "Object subclass: #TwoGuardW. "
        "TwoGuardW >> run "
        "  ^ [ Warning signal: 'w' ] "
        "      on: Error   do: [ :e | 1 ] "
        "      on: Warning do: [ :e | 2 ]. "
        "x := TwoGuardW newChild. "
        "x run.";
    {
        protoST::STRuntime rt;
        auto* r = runSrc(rt, errSrc);
        REQUIRE(r->asLong(rt.rootCtx()) == 1);
    }
    {
        protoST::STRuntime rt;
        auto* r = runSrc(rt, warnSrc);
        REQUIRE(r->asLong(rt.rootCtx()) == 2);
    }
}

TEST_CASE("EXC-b: an unhandled Warning resumes with nil — no abort",
          "[exceptions][track1]") {
    // No handler for the Warning. The refined default action prints and
    // resumes with nil: `signal` returns nil, the post-signal `123` runs, and
    // the top level yields 123 instead of aborting.
    const char* src =
        "Warning signal: 'just-a-warning'. "
        "123.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 123);
}

TEST_CASE("EXC-b: an unhandled base Exception resumes with nil",
          "[exceptions][track1]") {
    // The bare resumable `Exception` base, unhandled, resumes with nil too.
    const char* src =
        "Exception signal: 'base'. "
        "55.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 55);
}

TEST_CASE("EXC-b: an unhandled Error still aborts (non-resumable)",
          "[exceptions][track1]") {
    // The non-resumable Error keeps the EXC-a default action: abort.
    const char* src =
        "Error signal: 'still-fatal'. "
        "1.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(runSrc(rt, src),
                        Catch::Matchers::ContainsSubstring("still-fatal"));
}

TEST_CASE("EXC-b: a signal inside a handler still escapes to an outer handler",
          "[exceptions][track1]") {
    // Regression check from EXC-a, re-verified after the signal-loop
    // restructure: a handler that itself signals escapes outward.
    const char* src =
        "Object subclass: #InHandler2. "
        "InHandler2 >> run "
        "  ^ [ [ Error signal: 'first' ] "
        "        on: Error do: [ :e | Error signal: 'second' ] ] "
        "      on: Error do: [ :e | e messageText ]. "
        "i := InHandler2 newChild. "
        "i run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "second");
}

// ---------------------------------------------------------------------------
// EXC-c: ensure: / ifCurtailed: — cleanup blocks.
//
// A cleanup block cannot read the enclosing method's `self` or locals (block
// closures currently run with self == PROTO_NONE and capture nothing — a
// deferred F3 gap). Each test therefore observes the cleanup's effect through
// an attribute on a globally-named prototype, the same pattern EXC-b's `retry`
// test uses.
// ---------------------------------------------------------------------------

TEST_CASE("EXC-c: ensure: runs the cleanup on normal completion and yields the "
          "protected block's value",
          "[exceptions][track1]") {
    // The protected block returns 7; the cleanup flips `Flag1 messageText`
    // from 'pending' to 'ran'. A second statement reads the flag back to
    // prove the cleanup ran.
    const char* src =
        "Error subclass: #Flag1. "
        "Flag1 messageText: 'pending'. "
        "Object subclass: #EnsureCalm. "
        "EnsureCalm >> run "
        "  ^ [ 7 ] ensure: [ Flag1 messageText: 'ran' ]. "
        "c := EnsureCalm newChild. "
        "v := c run. "
        "Flag1 messageText.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "ran");
}

TEST_CASE("EXC-c: ensure: yields the protected block's value, not the cleanup's",
          "[exceptions][track1]") {
    // `[ 7 ] ensure: [ 99 ]` yields 7 — the cleanup's value is discarded.
    const char* src =
        "Object subclass: #EnsureVal. "
        "EnsureVal >> run "
        "  ^ [ 7 ] ensure: [ 99 ]. "
        "v := EnsureVal newChild. "
        "v run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("EXC-c: ensure: runs the cleanup on an abnormal exit — a signal "
          "unwinding through it",
          "[exceptions][track1]") {
    // An `ensure:` sits BETWEEN a `signal` and the `on: Error do:` that
    // catches it. The signal's UnwindToHandler unwinds through the `ensure:`,
    // which runs its cleanup (sets `Flag2 messageText` to 'ran') before
    // re-propagating to the outer handler.
    const char* src =
        "Error subclass: #Flag2. "
        "Flag2 messageText: 'pending'. "
        "Object subclass: #EnsureUnwind. "
        "EnsureUnwind >> run "
        "  ^ [ [ Error signal: 'boom' ] "
        "        ensure: [ Flag2 messageText: 'ran' ] ] "
        "      on: Error do: [ :e | e messageText ]. "
        "u := EnsureUnwind newChild. "
        "caught := u run. "
        "Flag2 messageText.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "ran");
}

TEST_CASE("EXC-c: ensure: runs the cleanup when a non-local return unwinds "
          "through the protected block",
          "[exceptions][track1]") {
    // The protected block contains `^42` — a non-local return from the home
    // method `run`. The NonLocalReturn unwinds through the `ensure:`, which
    // runs its cleanup (sets `Flag3 messageText` to 'ran').
    const char* src =
        "Error subclass: #Flag3. "
        "Flag3 messageText: 'pending'. "
        "Object subclass: #EnsureNLR. "
        "EnsureNLR >> run "
        "  [ ^ 42 ] ensure: [ Flag3 messageText: 'ran' ]. "
        "  ^ 0. "
        "n := EnsureNLR newChild. "
        "v := n run. "
        "Flag3 messageText.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "ran");
}

TEST_CASE("EXC-c: the non-local return through an ensure: still returns from "
          "the home method",
          "[exceptions][track1]") {
    // Same shape as above, but reads back the method's RESULT — `^42` inside
    // the protected block must still return 42 from `run` (the `^0` after the
    // `ensure:` is abandoned), proving the cleanup did not swallow the unwind.
    const char* src =
        "Error subclass: #Flag3b. "
        "Flag3b messageText: 'pending'. "
        "Object subclass: #EnsureNLRv. "
        "EnsureNLRv >> run "
        "  [ ^ 42 ] ensure: [ Flag3b messageText: 'ran' ]. "
        "  ^ 0. "
        "n := EnsureNLRv newChild. "
        "n run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("EXC-c: ifCurtailed: does NOT run the cleanup on normal completion",
          "[exceptions][track1]") {
    // The protected block completes normally with 5; `ifCurtailed:`'s cleanup
    // is SKIPPED, so `Flag4 messageText` stays 'pending'.
    const char* src =
        "Error subclass: #Flag4. "
        "Flag4 messageText: 'pending'. "
        "Object subclass: #CurtCalm. "
        "CurtCalm >> run "
        "  ^ [ 2 + 3 ] ifCurtailed: [ Flag4 messageText: 'ran' ]. "
        "c := CurtCalm newChild. "
        "v := c run. "
        "Flag4 messageText.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "pending");
}

TEST_CASE("EXC-c: ifCurtailed: yields the protected block's value on normal "
          "completion",
          "[exceptions][track1]") {
    // `[ 5 ] ifCurtailed: [ ... ]` yields 5 — the normal path returns the
    // protected block's value untouched.
    const char* src =
        "Object subclass: #CurtVal. "
        "CurtVal >> run "
        "  ^ [ 2 + 3 ] ifCurtailed: [ 99 ]. "
        "v := CurtVal newChild. "
        "v run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 5);
}

TEST_CASE("EXC-c: ifCurtailed: DOES run the cleanup on an abnormal exit",
          "[exceptions][track1]") {
    // A `signal` unwinds through the `ifCurtailed:` to an outer handler; the
    // abnormal exit fires the cleanup, setting `Flag5 messageText` to 'ran'.
    const char* src =
        "Error subclass: #Flag5. "
        "Flag5 messageText: 'pending'. "
        "Object subclass: #CurtUnwind. "
        "CurtUnwind >> run "
        "  ^ [ [ Error signal: 'boom' ] "
        "        ifCurtailed: [ Flag5 messageText: 'ran' ] ] "
        "      on: Error do: [ :e | e messageText ]. "
        "u := CurtUnwind newChild. "
        "caught := u run. "
        "Flag5 messageText.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "ran");
}

TEST_CASE("EXC-c: nested ensure: — both cleanups run, innermost first",
          "[exceptions][track1]") {
    // Two nested `ensure:` blocks around a `signal` caught by an outer
    // handler. As the UnwindToHandler propagates outward, the INNER ensure:'s
    // catch runs first, then the OUTER's. Each cleanup APPENDS a marker to
    // `Order messageText`: the inner cleanup writes 'I', the outer 'O', so the
    // final string 'IO' proves innermost-first ordering.
    const char* src =
        "Error subclass: #Order. "
        "Order messageText: ''. "
        "Object subclass: #NestEnsure. "
        "NestEnsure >> run "
        "  ^ [ [ [ Error signal: 'boom' ] "
        "          ensure: [ Order messageText: (Order messageText , 'I') ] ] "
        "        ensure: [ Order messageText: (Order messageText , 'O') ] ] "
        "      on: Error do: [ :e | e messageText ]. "
        "n := NestEnsure newChild. "
        "caught := n run. "
        "Order messageText.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asString(rt.rootCtx()) != nullptr);
    REQUIRE(r->asString(rt.rootCtx())->toStdString(rt.rootCtx()) == "IO");
}
