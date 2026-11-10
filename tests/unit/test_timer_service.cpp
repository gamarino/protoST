// TimerService (protoST 0.9.0, Task 7): the one timer ProtoThread that settles
// Futures at a deadline on the monotonic clock.
//
// What these cases pin:
//   1. The timer thread never runs Smalltalk code. A thenDo: callback fired by
//      a timer runs on a pool worker, so a callback that blocks its worker
//      does not hold up a second timer: the callback is released only after
//      that timer has fired, so the case completes only if the timer fired
//      while the callback was still blocking (otherwise it never ends).
//      Needs at least two workers (PROTOST_WORKERS is raised to 2 when lower).
//   2. A settled timer leaves nothing rooted: the pending-entry count returns
//      to zero once the timer has fired.
//   3. First completion wins: a Future resolved before its timer fires stays
//      resolved with its own value.
//   4. Pending timers never keep the runtime alive: a runtime destroyed with
//      timers due in an hour is destroyed promptly, and the timers are dropped.
//
// Timing: case 1 asserts a lower bound (the timer cannot fire early) and its
// order (ruling S14: no pass condition depends on scheduling); case 4 bounds
// the destruction at 5 s against an hour. No case uses a sleep to
// synchronise: every wait is on a Future or an Atom set by the program.

#include <catch2/catch_all.hpp>
#include "PosixEnv.h"

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TimerService.h"
#include "protoCore.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

// Compiled once; the module must outlive the runtime when blocks it defines
// can still run on a worker after the top level returned (case 1's callback).
std::unique_ptr<protoST::BytecodeModule> compile(const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return bc;
}

// At least two pool workers: one to be blocked by a callback, one to run the
// next timer's settle.
void requireTwoWorkers() {
    const char* env = std::getenv("PROTOST_WORKERS");
    if (!env || std::atoi(env) < 2) setenv("PROTOST_WORKERS", "2", 1);
}

} // namespace

TEST_CASE("TimerService: a blocking callback does not delay a later timer",
          "[timers][actors][parallel]") {
    requireTwoWorkers();
    // `blocker`'s callback announces that it started (resolving `started`)
    // and then blocks its worker until `release` is set (polling with
    // sleep:, which blocks the worker and helps no other work). Only once it
    // is known to be blocking does the program start a 50 ms timer and wait
    // for it; only after that wait returns does it set `release`. So the
    // program ends only if the timer fired while the callback was blocking:
    // a timer that waited for the callback would never fire.
    auto bc = compile(
        "| started finished release blocker elapsed | "
        "started := Future new. "
        "finished := Future new. "
        "release := Atom on: false. "
        "blocker := Future new. "
        "blocker thenDo: [:x | started resolve: true. "
        "  [release value] whileFalse: [Object sleep: 1]. "
        "  finished resolve: true]. "
        "blocker __resolveNilAfter: 0. "
        "started wait. "
        "elapsed := Time millisecondsToRun: [(Future after: 50) wait]. "
        "release swap: [:old | true]. "
        "finished wait. "
        "elapsed");
    protoST::STRuntime rt;
    REQUIRE(rt.workerCount() >= 2);
    const proto::ProtoObject* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->isInteger(rt.rootCtx()));
    const long long elapsed = r->asLong(rt.rootCtx());
    INFO("the 50 ms timer settled after " << elapsed << " ms");
    // Reaching here is the property: the timer fired while the callback still
    // blocked its worker (the callback was released only afterwards).
    CHECK(elapsed >= 50);
}

TEST_CASE("TimerService: a fired timer leaves no pending entry",
          "[timers][actors]") {
    auto bc = compile("(Future after: 10) wait. 1");
    protoST::STRuntime rt;
    REQUIRE(rt.timerService().pendingCount(rt.rootCtx()) == 0);
    const proto::ProtoObject* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 1);
    // The entry is removed before the settle task is enqueued, so by the time
    // the wait returned it is gone.
    CHECK(rt.timerService().pendingCount(rt.rootCtx()) == 0);
}

TEST_CASE("TimerService: a Future settled first keeps its own value",
          "[timers][actors]") {
    // The 30 ms timer would reject `f` with a TimeoutError; `f` is resolved
    // first. The 80 ms timer is popped after the 30 ms one (deadline order),
    // so by the time its wait returns the first timer has been dealt with.
    auto bc = compile(
        "| f | "
        "f := Future new. "
        "f __rejectTimeoutAfter: 30. "
        "f resolve: 41. "
        "(Future after: 80) wait. "
        "f wait + 1");
    protoST::STRuntime rt;
    const proto::ProtoObject* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    CHECK(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("TimerService: pending timers never keep the runtime alive",
          "[timers][actors]") {
    auto bc = compile(
        "Future after: 3600000. "
        "(Future new) within: 3600000. "
        "1");
    const auto start = std::chrono::steady_clock::now();
    {
        protoST::STRuntime rt;
        const proto::ProtoObject* r = rt.runTopLevel(*bc);
        REQUIRE(r != nullptr);
        REQUIRE(rt.timerService().pendingCount(rt.rootCtx()) == 2);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    INFO("construction, run and destruction took " << ms << " ms");
    CHECK(ms < 5000);
}

TEST_CASE("TimerService: a Future settled first stops being rooted at once",
          "[timers][actors]") {
    // Two one-minute timers whose Futures settle at once by other means: the
    // within: answer (its receiver resolves) and an after: Future resolved by
    // hand. Each settle cancels its timer, so nothing stays rooted although
    // both deadlines are a minute away. Without the cancellation the count
    // would stay 2 until the deadlines.
    auto bc = compile(
        "| f d g | "
        "f := Future new. "
        "d := f within: 60000. "
        "g := Future after: 60000. "
        "f resolve: 1. "
        "g resolve: 2. "
        "d wait + g wait");
    protoST::STRuntime rt;
    const proto::ProtoObject* r = rt.runTopLevel(*bc);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);
    CHECK(rt.timerService().pendingCount(rt.rootCtx()) == 0);
}
