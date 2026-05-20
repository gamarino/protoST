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
