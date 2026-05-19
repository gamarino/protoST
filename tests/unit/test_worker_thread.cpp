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
