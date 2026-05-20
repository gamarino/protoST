// Track 2, slice a (COL-a) — the collection foundation.
//
// Exercises the `Array` base operations (`size`, `at:`, `at:put:`, `do:`,
// class-side `new:` / `withAll:`), the shared derived iteration protocol on
// `Collection` (`collect:`, `select:`/`reject:`, `detect:`/`detect:ifNone:`,
// `inject:into:`, `do:separatedBy:`, `count:`, `,`, `isEmpty`), and the
// `#(...)` / `{...}` literal lowering.
//
// Every iteration test uses a block that closes over a method local or
// argument — exercising the just-landed closure-capture path.
//
// See docs/superpowers/specs/2026-05-20-collections.md.

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

TEST_CASE("COL-a: #(...) literal — size and at:", "[collections][track2]") {
    protoST::STRuntime rt;
    {
        auto* r = runSrc(rt, "#(1 2 3) size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 3);
    }
    {
        auto* r = runSrc(rt, "#(10 20 30) at: 2.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 20);
    }
}

TEST_CASE("COL-a: at:put: mutates the slot then at: reads it",
          "[collections][track2]") {
    // at:put: replaces element 2; the subsequent at: 2 reads the new value.
    const char* src =
        "Object subclass: #Box. "
        "Box >> run "
        "  | a | "
        "  a := #(10 20 30). "
        "  a at: 2 put: 99. "
        "  ^ a at: 2. "
        "b := Box newChild. b run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("COL-a: at:put: returns the stored value", "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "{ 1. 2. 3 } at: 1 put: 7.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 7);
}

TEST_CASE("COL-a: { ... } dynamic literal evaluates its expressions",
          "[collections][track2]") {
    // { 1+1. 2*2. 3 } → an Array of 2, 4, 3. inject:into: sums it to 9.
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "{ 1 + 1. 2 * 2. 3 } inject: 0 into: [ :a :b | a + b ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 9);
}

TEST_CASE("COL-a: do: sums elements into a method-local accumulator",
          "[collections][track2]") {
    // The do: block closes over the method temp `sum` — exercises the
    // just-fixed closure-capture path.
    const char* src =
        "Object subclass: #Summer. "
        "Summer >> total "
        "  | sum | "
        "  sum := 0. "
        "  #(4 5 6) do: [ :each | sum := sum + each ]. "
        "  ^ sum. "
        "s := Summer newChild. s total.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 15);
}

TEST_CASE("COL-a: collect: maps each element", "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt,
        "(#(1 2 3) collect: [ :x | x * 10 ]) "
        "  inject: 0 into: [ :a :b | a + b ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 60);
}

TEST_CASE("COL-a: select: and reject: filter", "[collections][track2]") {
    protoST::STRuntime rt;
    {
        // select: even elements of #(1 2 3 4 5 6) → 2 4 6 → size 3.
        auto* r = runSrc(rt,
            "(#(1 2 3 4 5 6) select: [ :x | (x / 2) * 2 = x ]) size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 3);
    }
    {
        // reject: even → keeps odd 1 3 5 → sum 9.
        auto* r = runSrc(rt,
            "(#(1 2 3 4 5 6) reject: [ :x | (x / 2) * 2 = x ]) "
            "  inject: 0 into: [ :a :b | a + b ].");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 9);
    }
}

TEST_CASE("COL-a: detect: finds the first matching element",
          "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "#(3 7 11 14) detect: [ :x | x > 8 ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 11);
}

TEST_CASE("COL-a: detect:ifNone: falls back when nothing matches",
          "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt,
        "#(1 2 3) detect: [ :x | x > 100 ] ifNone: [ 0 - 1 ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == -1);
}

TEST_CASE("COL-a: detect: with no match signals an Error catchable by on:do:",
          "[collections][track2]") {
    // detect: throws when nothing matches; the Error is caught by on:Error do:.
    const char* src =
        "Object subclass: #Finder. "
        "Finder >> run "
        "  ^ [ #(1 2 3) detect: [ :x | x > 100 ] ] "
        "      on: Error do: [ :e | 777 ]. "
        "f := Finder newChild. f run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 777);
}

TEST_CASE("COL-a: inject:into: folds with a block over a method argument",
          "[collections][track2]") {
    // The fold block adds `each` plus a method ARGUMENT `bias` — exercising
    // capture of a method argument inside an iteration block.
    const char* src =
        "Object subclass: #Folder. "
        "Folder >> sumWithBias: bias "
        "  ^ #(1 2 3) inject: 0 into: [ :acc :each | acc + each + bias ]. "
        "f := Folder newChild. f sumWithBias: 10.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    // (0+1+10) + (11+2+10) + (23+3+10) = 11 + 23 + 36 = ... fold:
    //   acc0=0 → 0+1+10=11 → 11+2+10=23 → 23+3+10=36
    REQUIRE(r->asLong(rt.rootCtx()) == 36);
}

TEST_CASE("COL-a: do:separatedBy: runs the separator between elements",
          "[collections][track2]") {
    // The element block adds each value; the separator adds 100. Three
    // elements → two separators. sum = (1+2+3) + 2*100 = 206.
    const char* src =
        "Object subclass: #Joiner. "
        "Joiner >> run "
        "  | sum | "
        "  sum := 0. "
        "  #(1 2 3) do: [ :x | sum := sum + x ] "
        "           separatedBy: [ sum := sum + 100 ]. "
        "  ^ sum. "
        "j := Joiner newChild. j run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 206);
}

TEST_CASE("COL-a: count: counts matching elements", "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "#(1 2 3 4 5) count: [ :x | x > 2 ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 3);
}

TEST_CASE("COL-a: anySatisfy: and allSatisfy:", "[collections][track2]") {
    protoST::STRuntime rt;
    {
        auto* r = runSrc(rt, "#(1 2 3) anySatisfy: [ :x | x > 2 ].");
        REQUIRE(r == PROTO_TRUE);
    }
    {
        auto* r = runSrc(rt, "#(1 2 3) allSatisfy: [ :x | x > 2 ].");
        REQUIRE(r == PROTO_FALSE);
    }
    {
        auto* r = runSrc(rt, "#(3 4 5) allSatisfy: [ :x | x > 2 ].");
        REQUIRE(r == PROTO_TRUE);
    }
}

TEST_CASE("COL-a: , concatenates two collections", "[collections][track2]") {
    protoST::STRuntime rt;
    {
        auto* r = runSrc(rt, "(#(1 2) , #(3 4 5)) size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 5);
    }
    {
        auto* r = runSrc(rt,
            "(#(1 2) , #(3 4)) inject: 0 into: [ :a :b | a + b ].");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 10);
    }
}

TEST_CASE("COL-a: isEmpty / notEmpty", "[collections][track2]") {
    protoST::STRuntime rt;
    {
        auto* r = runSrc(rt, "#() isEmpty.");
        REQUIRE(r == PROTO_TRUE);
    }
    {
        auto* r = runSrc(rt, "#(1) isEmpty.");
        REQUIRE(r == PROTO_FALSE);
    }
    {
        auto* r = runSrc(rt, "#(1 2) notEmpty.");
        REQUIRE(r == PROTO_TRUE);
    }
}

TEST_CASE("COL-a: at: out of range signals an Error caught by on:do:",
          "[collections][track2]") {
    const char* src =
        "Object subclass: #Bounds. "
        "Bounds >> run "
        "  ^ [ #(10 20 30) at: 99 ] on: Error do: [ :e | 555 ]. "
        "b := Bounds newChild. b run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 555);
}

TEST_CASE("COL-a: empty #() and empty {} literals", "[collections][track2]") {
    protoST::STRuntime rt;
    {
        auto* r = runSrc(rt, "#() size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 0);
    }
    {
        auto* r = runSrc(rt, "{} size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 0);
    }
}

TEST_CASE("COL-a: Array class>>new: builds a sized Array of nils",
          "[collections][track2]") {
    protoST::STRuntime rt;
    {
        auto* r = runSrc(rt, "(Array new: 4) size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 4);
    }
    {
        // Elements default to nil.
        auto* r = runSrc(rt, "(Array new: 3) at: 1.");
        REQUIRE(r == PROTO_NONE);
    }
}

TEST_CASE("COL-a: Array class>>withAll: copies another collection",
          "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt,
        "(Array withAll: #(7 8 9)) inject: 0 into: [ :a :b | a + b ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 24);
}

TEST_CASE("COL-a: asArray round-trips a collection", "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "#(1 2 3 4) asArray size.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 4);
}

TEST_CASE("COL-a: #(...) admits chars, strings and symbols",
          "[collections][track2]") {
    // Mixed literal — verifies the parser's literal-only rule lowers via
    // PUSH_CONST for each element kind.
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "#(1 $a 'str' #sym) size.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 4);
}
