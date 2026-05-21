// Track 3, sub-slice T3-b — multiple inheritance / mixins.
//
// Pins the `uses:` definition form and its semantics at the source -> parse
// -> compile -> run level, close to the cause, complementing the black-box
// conformance programs under tests/conformance/04-object-model/mixin-*.st.
//
// What is verified:
//   * a class assembled with two mixins understands a method from each;
//   * diamond resolution — a selector reachable via two mixins resolves to
//     the first in listed order;
//   * `super` from a multiply-inheriting class searches the primary
//     superclass subtree before the mixin subtrees;
//   * `super` reaches a mixin subtree when the primary superclass lacks the
//     selector;
//   * `subclass:instanceVariableNames:uses:` — own ivars plus mixin ivars;
//   * `subclass:uses:` works with an expression receiver;
//   * single-inheritance `super` is unchanged.
//
// All assertions share one STRuntime: protoST is single-runtime-per-process
// (STATUS.md D2), so the whole T3-b surface is exercised in one TEST_CASE.

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

TEST_CASE("T3-b: multiple inheritance and mixins via uses:",
          "[object-model][track3][t3b]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    SECTION("two mixins each contribute a method, both callable") {
        auto* r = runSrc(rt,
            "Object subclass: #PingMixin."
            "PingMixin >> ping  ^ 1."
            "Object subclass: #PongMixin."
            "PongMixin >> pong  ^ 2."
            "Object subclass: #Combo uses: { PingMixin. PongMixin }."
            "(Combo new ping) + (Combo new pong).");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 3);
    }

    SECTION("diamond resolves to the first mixin in listed order") {
        auto* r = runSrc(rt,
            "Object subclass: #DiaA."
            "DiaA >> which  ^ 10."
            "Object subclass: #DiaB."
            "DiaB >> which  ^ 20."
            "Object subclass: #DiaUser uses: { DiaA. DiaB }."
            "DiaUser new which.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 10);
    }

    SECTION("super searches the primary superclass before the mixins") {
        // Both the primary superclass and the mixin define `score`; a
        // `super score` from the subclass must reach the primary's.
        auto* r = runSrc(rt,
            "Object subclass: #PrimBase."
            "PrimBase >> score  ^ 100."
            "Object subclass: #ScoreMixin."
            "ScoreMixin >> score  ^ 999."
            "PrimBase subclass: #Player uses: { ScoreMixin }."
            "Player >> score  ^ 1 + super score."
            "Player new score.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 101);
    }

    SECTION("super reaches a mixin when the primary lacks the selector") {
        auto* r = runSrc(rt,
            "Object subclass: #BarePrim."
            "Object subclass: #PowerMixin."
            "PowerMixin >> power  ^ 7."
            "BarePrim subclass: #Gadget uses: { PowerMixin }."
            "Gadget >> power  ^ 1 + super power."
            "Gadget new power.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 8);
    }

    SECTION("subclass:instanceVariableNames:uses: — own and mixin ivars") {
        // The mixin declares `slot` and reads/writes it; the using class
        // declares its own `extra`. An instance must carry both.
        auto* r = runSrc(rt,
            "Object subclass: #SlotMixin instanceVariableNames: 'slot'."
            "SlotMixin >> slot: v  slot := v. ^ self."
            "SlotMixin >> slot  ^ slot."
            "Object subclass: #Cell instanceVariableNames: 'extra' "
            "  uses: { SlotMixin }."
            "Cell >> extra: v  extra := v. ^ self."
            "Cell >> extra  ^ extra."
            "Cell >> total  ^ self slot + self extra."
            "((Cell new slot: 30) extra: 12) total.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 42);
    }

    SECTION("subclass:uses: works with an expression receiver") {
        auto* r = runSrc(rt,
            "Object subclass: #ValMixin."
            "ValMixin >> val  ^ 55."
            "Object subclass: #Provider."
            "Provider >> classObj  ^ Object."
            "(Provider new classObj) subclass: #Built uses: { ValMixin }."
            "Built new val.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 55);
    }

    SECTION("regression: single-inheritance super is unchanged") {
        auto* r = runSrc(rt,
            "Object subclass: #SiBase."
            "SiBase >> base  ^ 4."
            "SiBase subclass: #SiSub."
            "SiSub >> base  ^ 3 + super base."
            "SiSub new base.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 7);
    }

    SECTION("a mixin's own super keeps single-inheritance semantics") {
        // Loud subclasses Animal; used as a mixin, Loud's `super` still
        // resolves up Loud's own definition chain (to Animal).
        auto* r = runSrc(rt,
            "Object subclass: #SoundBase."
            "SoundBase >> level  ^ 1."
            "SoundBase subclass: #LoudOne."
            "LoudOne >> level  ^ 10 + super level."
            "Object subclass: #Speaker uses: { LoudOne }."
            "Speaker new level.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 11);
    }
}
