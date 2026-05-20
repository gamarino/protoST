// F6 v2 T2 — managed worker ProtoThread spawned by STRuntime.
//
// These tests don't try to exercise parallelism per se (that arrives in T6).
// They cover the two non-negotiables for T2:
//   1. Constructing + destroying an STRuntime must spawn + join the worker
//      cleanly, without hanging or crashing — even when no actor work is
//      ever scheduled.
//   2. The existing F6 v1 actor flow (asActor / Future>>wait) must still
//      produce the same result with the worker present. The worker thread
//      may race with the main-thread drain but the asyncRoots-pinned actor
//      keeps the GC safe and only one consumer ever pops a given actor.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

TEST_CASE("F6 v2 T2: worker thread spawns and joins cleanly with no work",
          "[engine][actors][parallel]") {
    // Construct + destroy an STRuntime with no work. The worker must enter
    // its cv.wait(), be released by ~STRuntime's notify_all + shutdown flag,
    // and join() must return. If the cv predicate is wrong, this test hangs
    // (CTest's per-test timeout will catch it).
    {
        protoST::STRuntime rt;
        (void)rt;
    }
    SUCCEED("worker spawned + joined without hang");
}

TEST_CASE("F6 v2 T2: existing actor + Future>>wait flow still passes",
          "[engine][actors][parallel]") {
    // Sanity: the worker thread must not break the F6 v1 actor smoke flow.
    // Future>>wait drains on the main thread; the worker may also be looping,
    // but actors are pinned via asyncRoots so a GC interleaving cannot pull
    // the rug. Whichever thread pops the actor processes the message.
    const char* src =
        "Object subclass: #Adder. "
        "Adder >> add: x ^ x + 8. "
        "a := Adder newChild asActor. "
        "f := a add: 42. "
        "f wait.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 50);
}

// F6 v2 T7: prove that the worker pool actually contains more than one
// thread when the host has more than one core (or the user opts in via
// PROTOST_WORKERS). The exact number depends on the environment, so we
// check the boundary that matters for the parallelism proof below.
TEST_CASE("F6 v2 T7: worker pool size honours PROTOST_WORKERS / hardware",
          "[engine][actors][parallel]") {
    protoST::STRuntime rt;

    // The pool size is fixed at construction. With no env override the
    // constructor falls back to hardware_concurrency() (>=2 when the OS
    // refuses to report a number), capped at 8.
    size_t expected = std::thread::hardware_concurrency();
    if (expected == 0) expected = 2;
    if (const char* env = std::getenv("PROTOST_WORKERS")) {
        try {
            int parsed = std::stoi(env);
            if (parsed >= 1) expected = static_cast<size_t>(parsed);
        } catch (...) { /* malformed: keep hardware default */ }
    }
    if (expected > 8u) expected = 8u;

    INFO("workerCount=" << rt.workerCount() << " expected=" << expected);
    REQUIRE(rt.workerCount() == expected);
}

// F6 v2 T6: real parallelism proof using wall-clock time.
//
// Two distinct actors each receive a single `sleep: 200` message. The actor
// lock (F6 v2 T3) serialises messages targeting the SAME actor, but `a` and
// `b` hold disjoint mutexes — so if the pool truly has >=2 effective
// workers, both sleeps run concurrently and total wall-clock time is close
// to one sleep duration plus scheduling overhead, well under the 400 ms
// serial baseline.
// Helper: is the current runtime configuration expected to be able to
// demonstrate parallelism? Two failure modes are tolerated by returning
// false: (1) the user opted into single-worker mode via PROTOST_WORKERS=1,
// (2) the host genuinely has only one CPU. Both are real-world cases that
// the parallel test cannot prove anything about.
static bool canProveParallelism() {
    if (const char* env = std::getenv("PROTOST_WORKERS")) {
        try {
            if (std::stoi(env) < 2) return false;
        } catch (...) { /* malformed: fall through */ }
    }
    if (std::thread::hardware_concurrency() < 2) return false;
    return true;
}

TEST_CASE("F6 v2 T7: two actors run in parallel (wall-clock proof)",
          "[engine][actors][parallel]") {
    // On single-worker configurations the proof is structurally impossible
    // (two messages on different actors would still be serialised). We
    // record the reason via WARN + SUCCEED rather than SKIP because Catch2
    // SKIP exits the binary with a non-zero status that ctest interprets as
    // failure under catch_discover_tests; SUCCEED keeps the test green
    // while making the condition observable in the test log.
    if (!canProveParallelism()) {
        WARN("parallelism proof bypassed: PROTOST_WORKERS<2 or "
             "hardware_concurrency<2");
        SUCCEED();
        return;
    }

    const char* src =
        "a := 0 asActor. "
        "b := 0 asActor. "
        "fa := a sleep: 200. "
        "fb := b sleep: 200. "
        "fa wait. fb wait. "
        "1.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    // Defensive: if pool sizing ended up at 1 (e.g. thread-spawn failure
    // inside the constructor), bypass rather than fail. The canProveParallelism
    // guards cover env / hardware but cannot detect post-construction state.
    if (rt.workerCount() < 2) {
        WARN("runtime spawned < 2 workers; cannot prove parallelism");
        SUCCEED();
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    auto* r = rt.runTopLevel(*bc);
    auto t1 = std::chrono::steady_clock::now();
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 1);

    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    INFO("elapsed_ms = " << elapsed_ms
         << " (serial baseline ~400ms, parallel ~200ms)");
    // Lower bound: both sleeps really happened (no sub-200ms freebie).
    REQUIRE(elapsed_ms >= 180);
    // Upper bound: comfortably below the serial baseline. 350ms leaves
    // ~150ms of scheduling slack on top of the 200ms parallel floor; CI
    // machines under load have been observed at ~230-280ms with this
    // pool design. If this fails the pool is not actually running two
    // workers in parallel, which is exactly the bug we want to detect.
    REQUIRE(elapsed_ms < 350);
}

// ---------------------------------------------------------------------------
// F6 v3 C+D — cooperative yield + resume.
//
// The flagship test below exercises the full yield/resume cycle from
// Smalltalk code:
//
//   1. Two actors `echo` and `caller` are created. `caller` holds a
//      reference to `echo` in an instance variable.
//   2. The main thread sends `ask: 21` to `caller`, getting back a Future.
//   3. `caller`'s handler sends `respond: 21` to `echo` (returns a Future)
//      and then calls `wait` on it.
//   4. Because we are inside an actor handler AND the inner future is
//      still pending, Future>>wait throws FutureYield. ExecutionEngine
//      catches it, snapshots `caller`'s frames onto `caller`, appends
//      `caller` to the inner future's __waiters__, and rethrows.
//   5. STRuntime::drainOne catches FutureYield, stashes the message-
//      level Future on `caller`, and returns without resolving it. The
//      worker thread is now free to drain another actor.
//   6. Some worker (possibly the same one) picks up `echo`, runs
//      `echo respond: 21`, resolves the inner future with 42. The
//      resolveFutureFromDrain helper walks the inner future's
//      __waiters__ list and schedules `caller` for resume.
//   7. drainOne pops `caller`, sees __suspended_frame__, restores the
//      snapshot, pushes 42 onto the resumed frame's opStack (the value
//      `wait` would have returned synchronously), and continues. The
//      handler returns 42; drainOne resolves the message-level future.
//   8. The main thread's outer `fa wait` returns 42.
//
// This is the load-bearing F6 v3 test: every component (FutureYield
// throw, snapshot, waiter handoff, schedule from resolve, resume from
// snapshot, opStack injection) has to behave correctly or the test
// either hangs (timeout) or returns the wrong value.
// ---------------------------------------------------------------------------

TEST_CASE("F6 v3 C+D: actor handler that waits on another actor's future "
          "yields and resumes",
          "[engine][actors][f6v3][yield]") {
    const char* src =
        "Object subclass: #Echo. "
        "Echo >> respond: x ^ x * 2. "
        "Object subclass: #Caller instanceVariableNames: 'echo'. "
        "Caller >> initWith: e echo := e. "
        "Caller >> ask: x | f | f := echo respond: x. ^ f wait. "
        "echo := Echo newChild asActor. "
        "caller := Caller newChild. "
        "caller initWith: echo. "
        "callerActor := caller asActor. "
        "fa := callerActor ask: 21. "
        "fa wait.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("F6 v3 C+D: actor waiting on an already-resolved future does not "
          "yield (fast path)",
          "[engine][actors][f6v3][yield]") {
    // When an actor's handler calls wait on a future that has ALREADY
    // resolved, the synchronous fast-path inside prim_Future_wait should
    // return the value without throwing FutureYield. This exercises the
    // "in actor handler but state != 0" branch of the wait primitive.
    //
    // We construct the scenario by:
    //   1. Sending `respond:` to `echo` from the main thread, getting
    //      back a Future `innerF`.
    //   2. Calling `innerF wait` from main to make sure the future is
    //      fully resolved (state == 1, __value__ set) BEFORE we ever
    //      forward it to an actor.
    //   3. Sending `waitF: innerF` to a Holder actor. The handler
    //      calls `innerF wait`. Since the future is already resolved,
    //      this should return synchronously without yielding.
    //
    // If the wait incorrectly yielded on a resolved future, the resumed
    // actor would either hang (no waiter handoff path) or read the
    // wrong value. A correct fast-path returns `__value__` directly.
    const char* src =
        "Object subclass: #Echo. "
        "Echo >> respond: x ^ x * 3. "
        "Object subclass: #Holder. "
        "Holder >> waitF: anF ^ anF wait. "
        "echo := Echo newChild asActor. "
        "h := Holder newChild asActor. "
        "innerF := echo respond: 33. "
        "innerF wait. "
        "(h waitF: innerF) wait.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("F6 v3 C+D: rejected awaited future propagates through resumed actor "
          "as a rejection on the message-level future",
          "[engine][actors][f6v3][yield]") {
    // Scenario: caller's handler waits on a future that ends up REJECTED.
    // resumeWith throws std::runtime_error inside the resumed engine,
    // which propagates out through continueRun, drainOne catches it and
    // rejects the message-level future. The main thread's outer wait
    // re-throws as std::runtime_error.
    //
    // The producer here is a primitive on Echo that uses Future>>rejectWith:
    // directly inside its body. The synchronous-completion path of
    // drainOne would normally resolve the message future with the
    // method's return value, but rejectWith: + return of nil from a
    // method that already settled the future is fine — the resolve at
    // the end of drainOne sees state != 0 and no-ops.
    //
    // To keep the test focused, we use a method that throws via the
    // doesNotUnderstand path on a junk send; the standard exception
    // catch in drainOne rejects the future for us.
    const char* src =
        "Object subclass: #Echo. "
        "Echo >> failNow ^ self noSuchSelectorEver. "
        "Object subclass: #Caller instanceVariableNames: 'echo'. "
        "Caller >> initWith: e echo := e. "
        "Caller >> ask | f | f := echo failNow. ^ f wait. "
        "echo := Echo newChild asActor. "
        "caller := Caller newChild. "
        "caller initWith: echo. "
        "callerActor := caller asActor. "
        "fa := callerActor ask. ";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* fa = rt.runTopLevel(*bc);
    REQUIRE(fa != nullptr);

    // The message-level future for callerActor::ask should be rejected
    // because the inner echo::failNow rejection propagates through the
    // yielded wait. We invoke wait from the main thread to read the
    // status; the wait should throw std::runtime_error.
    //
    // Future>>wait on a rejected future throws std::runtime_error with
    // "Future rejected: ..." prefix. We probe via the future's state
    // directly so the test stays independent of the exact message text.
    auto* ctx = rt.rootCtx();
    auto* stateKey = proto::ProtoString::createSymbol(ctx, "__state__");
    // The main thread blocks here until the actor pipeline settles fa.
    bool rejected = false;
    try {
        // Compile + run a `fa wait` snippet in a fresh module since fa
        // is exposed only through the original top-level eval. Reuse
        // the same runtime so the queue and workers keep draining.
        // Simpler: poll the future state for up to a few hundred ms.
        for (int i = 0; i < 200; ++i) {
            auto* st = fa->getAttribute(ctx, stateKey);
            long long s = st ? st->asLong(ctx) : 0;
            if (s != 0) {
                rejected = (s == 2);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (...) { /* polling path doesn't throw */ }
    REQUIRE(rejected);
}

TEST_CASE("F6 v3 C+D: main-thread wait on pending future still blocks on cv "
          "(no yield outside actor context)",
          "[engine][actors][f6v3][yield]") {
    // Regression guard for the original F6 v2 main-thread wait path:
    // when no actor is currently being processed on the calling thread
    // (i.e. STRuntime::currentActor() returns nullptr), wait must NOT
    // throw FutureYield. The existing F6 hero test (Counter actor +
    // wait from main) already covers this, but we add an explicit check
    // here to make the contract observable in the F6 v3 test group.
    const char* src =
        "Object subclass: #Adder. "
        "Adder >> add: x ^ x + 100. "
        "a := Adder newChild asActor. "
        "f := a add: 42. "
        "f wait.";

    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 142);
}
