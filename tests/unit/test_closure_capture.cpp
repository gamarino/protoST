// CLO — closure capture in methods.
//
// A block created inside a *method* must see the method's arguments, local
// variables, `self`, and instance variables. Before this change a method
// never built a captured dict (slot 0 held only the module dict, if any) and
// blocks were invoked with self == PROTO_NONE, so every such reference read
// nil.
//
// Part 1 — `self` (and instance variables): PUSH_BLOCK stamps the creating
// frame's self onto the block as `__block_self__`; block invocation (both the
// direct value:/value: SEND fast-path and invokeBlock) builds the block frame
// with that self.
//
// Part 2 — method arguments and locals: a method whose inner blocks capture
// anything emits a MAKE_CAPTURED prologue that allocates a per-method captured
// dict in frame slot 0, and copies each captured argument's incoming value
// into it. Nested blocks reuse that one flat dict via PUSH_BLOCK's
// `__captured__` stamp.
//
// See docs/superpowers/specs/2026-05-20-closure-capture.md.

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

TEST_CASE("CLO: a block reads a method argument", "[engine][closures][clo]") {
    // `addOne:` runs a block that closes over the method argument `n`.
    // Before the fix the block's PUSH_CAPTURED found `n` nowhere (no method
    // captured dict) and read nil → nil + 1 would error or yield nil.
    const char* src =
        "Object subclass: #A. "
        "A >> addOne: n "
        "  ^ [ n + 1 ] value. "
        "a := A newChild. "
        "a addOne: 41.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("CLO: a block reads a method local", "[engine][closures][clo]") {
    // The block closes over the method temp `x` declared with `| x |`.
    const char* src =
        "Object subclass: #B. "
        "B >> compute "
        "  | x | "
        "  x := 10. "
        "  ^ [ x * 5 ] value. "
        "b := B newChild. "
        "b compute.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 50);
}

TEST_CASE("CLO: a block mutates a captured method local; the method observes "
          "the change", "[engine][closures][clo]") {
    // The block writes the captured method local `x`; after the block runs,
    // the method body reads the mutated value through the same captured dict.
    const char* src =
        "Object subclass: #C. "
        "C >> bump "
        "  | x | "
        "  x := 1. "
        "  [ x := x + 100 ] value. "
        "  ^ x. "
        "c := C newChild. "
        "c bump.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 101);
}

TEST_CASE("CLO: a block uses self", "[engine][closures][clo]") {
    // The block sends `identify` to `self` — the method's receiver. The block
    // frame's self is the `__block_self__` stamped by PUSH_BLOCK.
    const char* src =
        "Object subclass: #D. "
        "D >> identify ^ 7. "
        "D >> viaBlock ^ [ self identify ] value. "
        "d := D newChild. "
        "d viaBlock.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("CLO: a block reads and writes an instance variable",
          "[engine][closures][clo]") {
    // The block reads the inst var `count`, then a second block writes it;
    // the method observes the write. PUSH_INSTVAR / STORE_INSTVAR inside a
    // block resolve against the block frame's inherited self.
    const char* src =
        "Object subclass: #E instanceVariableNames: 'count'. "
        "E >> setup count := 5. "
        "E >> readIt ^ [ count ] value. "
        "E >> writeIt "
        "  [ count := count + 30 ] value. "
        "  ^ count. "
        "e := E newChild. "
        "e setup. "
        "r1 := e readIt. "
        "r2 := e writeIt. "
        "r1 + r2.";
    protoST::STRuntime rt;
    // readIt → 5; writeIt → 5 + 30 = 35; sum = 40.
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 40);
}

TEST_CASE("CLO: non-local return over a captured method argument "
          "(firstEven: shape)", "[engine][closures][clo][nlr]") {
    // A method whose block does `^x` over a captured method argument `x`.
    // This combines closure capture (the block must SEE `x`) with non-local
    // return (the `^` returns from the method). The trailing `^ 0` is dead.
    const char* src =
        "Object subclass: #F. "
        "F >> echo: x "
        "  [ ^ x ] value. "
        "  ^ 0. "
        "f := F newChild. "
        "f echo: 88.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 88);
}

TEST_CASE("CLO: a block run via invokeBlock (ifTrue:) sees args, locals, "
          "self and inst vars", "[engine][closures][clo]") {
    // ifTrue: evaluates its block through invokeBlock in a NESTED engine.
    // The block must still see the method argument `delta`, the method local
    // `base`, `self` (via the inst var) and the inst var `seed` itself —
    // exactly as the same-engine value: path does.
    const char* src =
        "Object subclass: #G instanceVariableNames: 'seed'. "
        "G >> seedWith: s seed := s. "
        "G >> mix: delta "
        "  | base | "
        "  base := 1000. "
        "  true ifTrue: [ ^ seed + delta + base + (self bonus) ]. "
        "  ^ 0. "
        "G >> bonus ^ 3. "
        "g := G newChild. "
        "g seedWith: 7. "
        "g mix: 20.";
    protoST::STRuntime rt;
    // 7 + 20 + 1000 + 3 = 1030.
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 1030);
}

TEST_CASE("CLO: a doubly-nested block reads a method local",
          "[engine][closures][clo]") {
    // block-in-block-in-method. The innermost block closes over the method
    // local `x`; both blocks share the one flat per-method captured dict.
    const char* src =
        "Object subclass: #H. "
        "H >> deep "
        "  | x | "
        "  x := 9. "
        "  ^ [ [ x * 4 ] value ] value. "
        "h := H newChild. "
        "h deep.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 36);
}

TEST_CASE("CLO: a doubly-nested block reads self and a method argument",
          "[engine][closures][clo]") {
    // The inner block uses both `self` (transitively inherited through the
    // outer block) and the captured method argument `k`.
    const char* src =
        "Object subclass: #I. "
        "I >> tag ^ 100. "
        "I >> wrap: k "
        "  ^ [ [ (self tag) + k ] value ] value. "
        "i := I newChild. "
        "i wrap: 11.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 111);
}

TEST_CASE("CLO: top-level module closures still work (regression)",
          "[engine][closures][clo]") {
    // The module top-level builds its captured dict via runTopLevel (no
    // MAKE_CAPTURED). A module-level block reading/writing a module local
    // must still resolve through that dict.
    const char* src =
        "total := 0. "
        "[ total := total + 5 ] value. "
        "[ total := total + 37 ] value. "
        "total.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r->asLong(rt.rootCtx()) == 42);
}

TEST_CASE("CLO: an actor handler's block captures its own args and self "
          "(regression of the F6 actor path)",
          "[engine][closures][clo][actors]") {
    // The actor handler `handle:` runs on a worker thread in its own engine.
    // Its block closes over the handler argument `amount`, the inst var
    // `balance`, and `self` (via a self-send) — all of which the worker-path
    // pushFrame must thread through correctly.
    const char* src =
        "Object subclass: #Account instanceVariableNames: 'balance'. "
        "Account >> openWith: b balance := b. "
        "Account >> fee ^ 2. "
        "Account >> handle: amount "
        "  ^ [ balance + amount + (self fee) ] value. "
        "acc := Account newChild. "
        "acc openWith: 50. "
        "actor := acc asActor. "
        "f := actor handle: 8. "
        "f wait.";
    protoST::STRuntime rt;
    // 50 + 8 + 2 = 60.
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 60);
}

TEST_CASE("CLO: a captured closure survives a cooperative yield and still "
          "resolves captured names and self after restore",
          "[engine][closures][clo][actors][yield]") {
    // The caller's handler `ask:` produces a pending Future (a send to another
    // actor), then runs a block via a direct value: SEND. The block closes
    // over the method argument `x`, the inst var `label`, and `self`. The
    // block's `wait` yields cooperatively (FutureYield): the handler's frames_
    // — including the captured dict in slot 0 and the block frame's self —
    // are snapshotted and later restored. After the resume the captured names
    // and self must still resolve correctly.
    const char* src =
        "Object subclass: #Doubler. "
        "Doubler >> twice: x ^ x * 2. "
        "Object subclass: #Asker "
        "  instanceVariableNames: 'peer label'. "
        "Asker >> initWith: p peer := p. "
        "Asker >> labelIs: l label := l. "
        "Asker >> bias ^ 1. "
        "Asker >> ask: x "
        "  | f | "
        "  f := peer twice: x. "
        "  ^ [ (f wait) + label + x + (self bias) ] value. "
        "doubler := Doubler newChild asActor. "
        "asker := Asker newChild. "
        "asker initWith: doubler. "
        "asker labelIs: 1000. "
        "askerActor := asker asActor. "
        "fa := askerActor ask: 21. "
        "fa wait.";
    protoST::STRuntime rt;
    // f wait = 42; + label 1000 + x 21 + bias 1 = 1064.
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 1064);
}
