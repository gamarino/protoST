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

// ====================  COL-b — OrderedCollection  ===========================
//
// The growable sequenceable collection: add:/addFirst:/addLast:/addAll:,
// removeFirst/removeLast/remove:, at:/at:put:, plus the inherited derived
// protocol, which now builds results from the receiver's per-class `species`.

TEST_CASE("COL-b: OrderedCollection new + add: grows and keeps order",
          "[collections][track2]") {
    const char* src =
        "Object subclass: #Builder. "
        "Builder >> run "
        "  | c | "
        "  c := OrderedCollection new. "
        "  c add: 10. c add: 20. c add: 30. "
        "  ^ (c size) + (c at: 1) + (c at: 3). "
        "b := Builder newChild. b run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 3 + 10 + 30);
}

TEST_CASE("COL-b: add: returns its argument", "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt, "OrderedCollection new add: 99.");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("COL-b: addFirst: prepends, addLast:/add: append",
          "[collections][track2]") {
    // addLast: 2, addFirst: 1, addLast: 3 → [1 2 3]; at: 1 is 1, at: 3 is 3.
    const char* src =
        "Object subclass: #Order. "
        "Order >> run "
        "  | c | "
        "  c := OrderedCollection new. "
        "  c addLast: 2. c addFirst: 1. c addLast: 3. "
        "  ^ ((c at: 1) * 100) + ((c at: 2) * 10) + (c at: 3). "
        "o := Order newChild. o run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 123);
}

TEST_CASE("COL-b: removeFirst / removeLast return the element and shrink",
          "[collections][track2]") {
    protoST::STRuntime rt;
    {
        // removeFirst answers the removed element.
        const char* src =
            "Object subclass: #RF. "
            "RF >> run "
            "  | c | "
            "  c := OrderedCollection new. "
            "  c add: 7. c add: 8. c add: 9. "
            "  ^ c removeFirst. "
            "x := RF newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 7);
    }
    {
        // removeLast answers the removed element; the collection then shrinks.
        const char* src =
            "Object subclass: #RL. "
            "RL >> run "
            "  | c last | "
            "  c := OrderedCollection new. "
            "  c add: 7. c add: 8. c add: 9. "
            "  last := c removeLast. "
            "  ^ (last * 10) + (c size). "
            "x := RL newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 92);  // last=9, size now 2
    }
}

TEST_CASE("COL-b: remove: removes a matching element", "[collections][track2]") {
    const char* src =
        "Object subclass: #Rm. "
        "Rm >> run "
        "  | c | "
        "  c := OrderedCollection new. "
        "  c add: 1. c add: 2. c add: 3. "
        "  c remove: 2. "
        "  ^ ((c size) * 100) + ((c at: 1) * 10) + (c at: 2). "
        "x := Rm newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 213);  // size 2, [1 3]
}

TEST_CASE("COL-b: remove:ifAbsent: fallback fires when absent",
          "[collections][track2]") {
    const char* src =
        "Object subclass: #RmIA. "
        "RmIA >> run "
        "  | c | "
        "  c := OrderedCollection new. "
        "  c add: 1. c add: 2. "
        "  ^ c remove: 99 ifAbsent: [ 0 - 1 ]. "
        "x := RmIA newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == -1);
}

TEST_CASE("COL-b: remove: of an absent element signals an Error",
          "[collections][track2]") {
    const char* src =
        "Object subclass: #RmErr. "
        "RmErr >> run "
        "  ^ [ | c | "
        "      c := OrderedCollection new. c add: 1. "
        "      c remove: 99 ] on: Error do: [ :e | 444 ]. "
        "x := RmErr newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 444);
}

TEST_CASE("COL-b: addAll: from an Array and from an OrderedCollection",
          "[collections][track2]") {
    protoST::STRuntime rt;
    {
        // addAll: an Array literal.
        const char* src =
            "Object subclass: #AAA. "
            "AAA >> run "
            "  | c | "
            "  c := OrderedCollection new. "
            "  c add: 1. "
            "  c addAll: #(2 3 4). "
            "  ^ c inject: 0 into: [ :a :b | a + b ]. "
            "x := AAA newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 10);
    }
    {
        // addAll: another OrderedCollection.
        const char* src =
            "Object subclass: #AAO. "
            "AAO >> run "
            "  | a b | "
            "  a := OrderedCollection new. a add: 1. a add: 2. "
            "  b := OrderedCollection new. b add: 3. b add: 4. "
            "  a addAll: b. "
            "  ^ a inject: 0 into: [ :x :y | x + y ]. "
            "z := AAO newChild. z run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 10);
    }
}

TEST_CASE("COL-b: at:put: replaces a slot", "[collections][track2]") {
    const char* src =
        "Object subclass: #AP. "
        "AP >> run "
        "  | c | "
        "  c := OrderedCollection new. "
        "  c add: 10. c add: 20. c add: 30. "
        "  c at: 2 put: 99. "
        "  ^ c at: 2. "
        "x := AP newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 99);
}

TEST_CASE("COL-b: at: out of range signals an Error", "[collections][track2]") {
    const char* src =
        "Object subclass: #OOR. "
        "OOR >> run "
        "  ^ [ | c | c := OrderedCollection new. c add: 1. "
        "      c at: 5 ] on: Error do: [ :e | 333 ]. "
        "x := OOR newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 333);
}

TEST_CASE("COL-b: removeFirst on an empty collection signals an Error",
          "[collections][track2]") {
    const char* src =
        "Object subclass: #Empty. "
        "Empty >> run "
        "  ^ [ OrderedCollection new removeFirst ] "
        "      on: Error do: [ :e | 222 ]. "
        "x := Empty newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 222);
}

TEST_CASE("COL-b: derived protocol works on an OrderedCollection",
          "[collections][track2]") {
    protoST::STRuntime rt;
    {
        // select: keeps elements > 2; inject:into: sums the result.
        const char* src =
            "Object subclass: #Sel. "
            "Sel >> run "
            "  | c | "
            "  c := OrderedCollection new. "
            "  c add: 1. c add: 2. c add: 3. c add: 4. "
            "  ^ (c select: [ :x | x > 2 ]) "
            "      inject: 0 into: [ :a :b | a + b ]. "
            "x := Sel newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 7);  // 3 + 4
    }
    {
        // detect: finds the first match.
        const char* src =
            "Object subclass: #Det. "
            "Det >> run "
            "  | c | "
            "  c := OrderedCollection new. "
            "  c add: 5. c add: 9. c add: 13. "
            "  ^ c detect: [ :x | x > 8 ]. "
            "x := Det newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 9);
    }
    {
        // do: sums into a method local closed over by the block.
        const char* src =
            "Object subclass: #DoSum. "
            "DoSum >> run "
            "  | c sum | "
            "  c := OrderedCollection new. "
            "  c add: 4. c add: 5. c add: 6. "
            "  sum := 0. "
            "  c do: [ :each | sum := sum + each ]. "
            "  ^ sum. "
            "x := DoSum newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 15);
    }
}

TEST_CASE("COL-b: collect: on an OrderedCollection yields an OrderedCollection",
          "[collections][track2]") {
    // Behavioural proof of species: the collect: result understands `add:`
    // (an OrderedCollection-only mutator) and grows.
    const char* src =
        "Object subclass: #Spec. "
        "Spec >> run "
        "  | c mapped | "
        "  c := OrderedCollection new. "
        "  c add: 1. c add: 2. c add: 3. "
        "  mapped := c collect: [ :x | x * 10 ]. "
        "  mapped add: 999. "
        "  ^ (mapped size) * 1000 + (mapped at: 1). "
        "x := Spec newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 4 * 1000 + 10);  // grew to 4; [10 20 30 999]

    // Identity proof: `species` answers the OrderedCollection class object.
    auto* sp = runSrc(rt, "(OrderedCollection new) species.");
    REQUIRE(sp == rt.bootstrap().orderedCollectionProto);
}

TEST_CASE("COL-b: species regression — anArray collect: still yields an Array",
          "[collections][track2]") {
    // `species` is per-class: an Array's species is the Array class object,
    // and a collect: over an Array therefore produces an Array.
    protoST::STRuntime rt;
    {
        auto* sp = runSrc(rt, "#(1 2 3) species.");
        REQUIRE(sp == rt.bootstrap().arrayProto);
    }
    {
        auto* sp = runSrc(rt, "(#(1 2 3) collect: [ :x | x * 2 ]) species.");
        REQUIRE(sp == rt.bootstrap().arrayProto);
    }
    {
        auto* sp = runSrc(rt,
            "((OrderedCollection withAll: #(1 2)) collect: [ :x | x ]) species.");
        REQUIRE(sp == rt.bootstrap().orderedCollectionProto);
    }
}

TEST_CASE("COL-b: asArray on an OrderedCollection yields an Array",
          "[collections][track2]") {
    protoST::STRuntime rt;
    {
        // Round-trip: size is preserved.
        const char* src =
            "Object subclass: #AsA. "
            "AsA >> run "
            "  | c | "
            "  c := OrderedCollection new. "
            "  c add: 1. c add: 2. c add: 3. c add: 4. "
            "  ^ c asArray size. "
            "x := AsA newChild. x run.";
        auto* r = runSrc(rt, src);
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(rt.rootCtx()) == 4);
    }
    {
        // asArray always yields an Array regardless of species — its `species`
        // is the Array class object even though the source was an OC.
        auto* sp = runSrc(rt,
            "((OrderedCollection withAll: #(1 2 3)) asArray) species.");
        REQUIRE(sp == rt.bootstrap().arrayProto);
    }
}

TEST_CASE("COL-b: OrderedCollection class>>withAll: builds from a collection",
          "[collections][track2]") {
    protoST::STRuntime rt;
    auto* r = runSrc(rt,
        "(OrderedCollection withAll: #(7 8 9)) "
        "  inject: 0 into: [ :a :b | a + b ].");
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 24);
}

TEST_CASE("COL-b: first / last convenience accessors", "[collections][track2]") {
    const char* src =
        "Object subclass: #FL. "
        "FL >> run "
        "  | c | "
        "  c := OrderedCollection new. "
        "  c add: 11. c add: 22. c add: 33. "
        "  ^ ((c first) * 100) + (c last). "
        "x := FL newChild. x run.";
    protoST::STRuntime rt;
    auto* r = runSrc(rt, src);
    REQUIRE(r != nullptr);
    REQUIRE(r->asLong(rt.rootCtx()) == 1133);
}
