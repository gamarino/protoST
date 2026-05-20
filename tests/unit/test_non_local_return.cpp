// Track 1, slice 1 — non-local return.
//
// `^expr` inside a block returns from the block's HOME method — the method
// activation in which the block was textually created — not merely from the
// block. These tests exercise both the same-engine path (a block invoked via
// a direct `value:` SEND, which runs as a frame in the SAME ExecutionEngine)
// and the nested-engine path (a block invoked via `ifTrue:`, which runs in a
// fresh ExecutionEngine via invokeBlock — so `^` must unwind ACROSS an engine
// boundary as a NonLocalReturn).
//
// See docs/superpowers/specs/2026-05-20-non-local-return.md.

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

TEST_CASE("NLR: ^ from a one-level block returns from the home method "
          "(value: path)",
          "[engine][nlr][track1]") {
    // `pick:` returns 99 from inside a block run via a direct `value:` SEND.
    // The `^99` must abandon `pick:` entirely — the trailing `^ 0` is dead.
    // The block runs as a frame in the SAME engine, so the RETURN handler
    // unwinds frames_ locally (no NonLocalReturn exception).
    const char* src =
        "Object subclass: #Chooser. "
        "Chooser >> pick: x "
        "  [ :y | ^ y ] value: x. "
        "  ^ 0. "
        "c := Chooser newChild. "
        "c pick: 99.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("NLR: ^ from a one-level block returns from the home method "
          "(ifTrue: / invokeBlock path)",
          "[engine][nlr][track1]") {
    // The block is invoked via `ifTrue:`, which evaluates it inside a nested
    // ExecutionEngine (block_prims.cpp::invokeBlock). The `^42` cannot find
    // its home frame in the nested engine, so it throws a NonLocalReturn that
    // bubbles past invokeBlock to the parent engine — which owns the home
    // method frame and unwinds to it. The trailing `^ 0` is dead.
    const char* src =
        "Object subclass: #Gate. "
        "Gate >> pass: flag "
        "  flag ifTrue: [ ^ 42 ]. "
        "  ^ 0. "
        "g := Gate newChild. "
        "g pass: true.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("NLR: a block whose ^ does not fire falls through to the method "
          "trailer",
          "[engine][nlr][track1]") {
    // When `flag` is false the `ifTrue:` block never runs, so no non-local
    // return happens and the method's own `^ 7` trailer produces the result.
    const char* src =
        "Object subclass: #Gate2. "
        "Gate2 >> pass: flag "
        "  flag ifTrue: [ ^ 42 ]. "
        "  ^ 7. "
        "g := Gate2 newChild. "
        "g pass: false.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("NLR: ^ from a doubly-nested block targets the home method",
          "[engine][nlr][track1]") {
    // The `^7` is two block-nesting levels deep. A block created inside
    // another block inherits the SAME method home (PUSH_BLOCK stamps the
    // creating frame's homeFrameId, which is itself a block frame carrying
    // the method's home). So `^7` returns from `deep`, not from either
    // enclosing block. Inner block via value:, outer via value: too.
    const char* src =
        "Object subclass: #Nest. "
        "Nest >> deep "
        "  [ :a | [ :b | ^ b ] value: a ] value: 7. "
        "  ^ 0. "
        "n := Nest newChild. "
        "n deep.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("NLR: doubly-nested block via mixed value: / ifTrue: still targets "
          "the home method",
          "[engine][nlr][track1]") {
    // The outer block runs via value: (same-engine frame); inside it an
    // `ifTrue:` evaluates an inner block in a NESTED engine. The inner
    // block's `^5` crosses both a same-engine block frame and an engine
    // boundary as a NonLocalReturn, yet still returns from `go` — the home
    // method — abandoning the dead `^ 0` trailer.
    const char* src =
        "Object subclass: #Mix. "
        "Mix >> go "
        "  [ true ifTrue: [ ^ 5 ] ] value. "
        "  ^ 0. "
        "m := Mix newChild. "
        "m go.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 5);
}

TEST_CASE("NLR: ^ in a block at method top level behaves like a normal "
          "method return",
          "[engine][nlr][track1]") {
    // The block IS the whole method body and its `^` fires immediately.
    // Result is identical to a plain method return.
    const char* src =
        "Object subclass: #Top. "
        "Top >> run "
        "  [ ^ 11 ] value. "
        "  ^ 0. "
        "t := Top newChild. "
        "t run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 11);
}

TEST_CASE("NLR: a RETURN_TOP block trailer returns only from the block",
          "[engine][nlr][track1]") {
    // The block's last statement has NO `^` — its implicit trailer is a
    // RETURN_TOP, which is always a LOCAL return: the block yields its last
    // value to `value`, and `done` then proceeds to its own `^` trailer.
    // If RETURN_TOP wrongly behaved like a non-local RETURN this would
    // return 3 (the block's value) instead of 100.
    const char* src =
        "Object subclass: #Trailer. "
        "Trailer >> done "
        "  | v | "
        "  v := [ 1 + 2 ] value. "
        "  ^ v + 97. "
        "x := Trailer newChild. "
        "x done.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 100);
}

TEST_CASE("NLR: top-level ^ inside a block returns from the top-level module",
          "[engine][nlr][track1]") {
    // The home of a block created at module scope is the top-level frame.
    // `^ 55` returns from the module; the trailing `0` is dead.
    const char* src =
        "true ifTrue: [ ^ 55 ]. "
        "0.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 55);
}

TEST_CASE("NLR: ^ inside an actor handler's ifTrue: block resolves the "
          "handler's Future",
          "[engine][nlr][track1][actors]") {
    // The actor handler `decide` runs in its own ExecutionEngine on a worker
    // thread. Its `ifTrue:` block does `^ 70`, a non-local return from
    // `decide` that crosses the invokeBlock nested-engine boundary as a
    // NonLocalReturn caught by the handler's engine. drainOne resolves the
    // message Future with that value; the main thread's `wait` observes 70.
    const char* src =
        "Object subclass: #Judge. "
        "Judge >> decide "
        "  true ifTrue: [ ^ 70 ]. "
        "  ^ 0. "
        "j := Judge newChild asActor. "
        "f := j decide. "
        "f wait.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 70);
}

TEST_CASE("NLR: ^ inside an actor handler's value: block resolves the "
          "handler's Future",
          "[engine][nlr][track1][actors]") {
    // Same as above but the handler's block is invoked via a direct `value:`
    // SEND — it runs as a frame in the handler's own engine, so the `^y`
    // unwinds frames_ locally (no NonLocalReturn exception). The block's own
    // parameter `y` carries the value, so no capture is involved.
    const char* src =
        "Object subclass: #Judge2. "
        "Judge2 >> decide: x "
        "  [ :y | ^ y ] value: x. "
        "  ^ 0. "
        "j := Judge2 newChild asActor. "
        "f := j decide: 33. "
        "f wait.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 33);
}

TEST_CASE("NLR: dead home — invoking a block whose home method already "
          "returned raises the dead-home error",
          "[engine][nlr][track1]") {
    // `makeBlock` returns a closure `[ ^ 1 ]` and then itself returns. By
    // the time the top-level invokes the escaped block, `makeBlock`'s home
    // frame is long gone. The `^1` finds no live home in any engine, so the
    // NonLocalReturn escapes the outermost engine and surfaces as the
    // dead-home runtime error.
    const char* src =
        "Object subclass: #Maker. "
        "Maker >> makeBlock "
        "  ^ [ ^ 1 ]. "
        "m := Maker newChild. "
        "blk := m makeBlock. "
        "blk value.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(
        runSrc(rt, src),
        Catch::Matchers::ContainsSubstring("home method has already returned"));
}

TEST_CASE("NLR: dead home inside an actor handler rejects the handler's "
          "Future",
          "[engine][nlr][track1][actors]") {
    // An actor handler invokes an escaped closure whose home method has
    // already returned. The dead-home NonLocalReturn escapes the handler's
    // engine; drainOne rejects the message Future with the dead-home error,
    // which the main thread's `wait` re-raises.
    const char* src =
        "Object subclass: #Escaper "
        "  instanceVariableNames: 'saved'. "
        "Escaper >> stash "
        "  saved := [ ^ 1 ]. "
        "  ^ 0. "
        "Escaper >> fire "
        "  ^ saved value. "
        "e := Escaper newChild. "
        "e stash. "
        "ea := e asActor. "
        "f := ea fire. "
        "f wait.";
    protoST::STRuntime rt;
    REQUIRE_THROWS_WITH(
        runSrc(rt, src),
        Catch::Matchers::ContainsSubstring("home method has already returned"));
}

TEST_CASE("NLR: non-local return survives a cooperative yield",
          "[engine][nlr][track1][actors][yield]") {
    // Caller's handler `ask:` calls `echo respond: x` (another actor),
    // producing a pending Future, then runs a block via a direct `value:`
    // SEND — the engine-frame path, which IS cooperatively yieldable. The
    // block does `^ (fut wait)`: the `wait` yields cooperatively
    // (FutureYield), and the handler's frames_ are snapshotted, INCLUDING
    // the new frameId / homeFrameId fields written by snapshotFrames. When
    // `echo` resolves the inner future the handler resumes from the restored
    // snapshot; the `^` then fires and must still find its home method frame
    // (`ask:`) by the un-renumbered global id. The non-local return carries
    // 2 * 21 == 42 out of `ask:`; the main thread's `wait` observes it.
    const char* src =
        "Object subclass: #Echo. "
        "Echo >> respond: x ^ x * 2. "
        "Object subclass: #Caller instanceVariableNames: 'echo'. "
        "Caller >> initWith: e echo := e. "
        "Caller >> ask: x "
        "  | f | "
        "  f := echo respond: x. "
        "  [ :fut | ^ (fut wait) ] value: f. "
        "  ^ 0. "
        "echo := Echo newChild asActor. "
        "caller := Caller newChild. "
        "caller initWith: echo. "
        "callerActor := caller asActor. "
        "fa := callerActor ask: 21. "
        "fa wait.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}
