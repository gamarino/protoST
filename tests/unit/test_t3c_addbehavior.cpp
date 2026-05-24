// Track 3, sub-slice T3-c — on-the-fly behaviour composition.
//
// Pins `addBehavior:` at the source -> parse -> compile -> run level, close to
// the cause, complementing the black-box conformance programs under
// tests/conformance/04-object-model/addbehavior-*.st.
//
// What is verified:
//   * `addBehavior:` adds a mixin's method to a class; an instance created
//     AFTER the call responds to it;
//   * the class object itself responds to the mixin's method;
//   * a class given a behaviour keeps its own methods and its `super` path;
//   * `addBehavior:` composes with a `uses:` class;
//   * the documented "future instances only" limitation — an instance created
//     BEFORE the call does NOT gain the mixin;
//   * `addParent:` is a lower-level alias for `addBehavior:`;
//   * regression — single inheritance and `uses:` are unaffected.
//
// THE PROTOCORE CONSTRAINT (probed directly): protoCore freezes an object's
// parent chain into its base cell at construction. `newChild` copies that
// frozen chain; a later `addParent`/`setParents` on the class is invisible to
// the class's instances — even instances created AFTER the mutation.
// `addBehavior:` therefore REBUILDS the class with the mixin baked into the
// base chain and rebinds the global; this is exercised end to end here.
//
// All assertions share one STRuntime: protoST is single-runtime-per-process
// (STATUS.md D2), so the whole T3-c surface is exercised in one TEST_CASE.

#include <catch2/catch_all.hpp>

#include <memory>
#include <vector>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

namespace {

// Compiled modules must outlive the run: a block closure embeds a `__bc_ptr__`
// into its defining BytecodeModule, so a module freed while a closure (e.g. an
// `on:do:` handler block) is still reachable would leave that closure dangling.
// The fixture owns every module it compiles for the process lifetime.
std::vector<std::unique_ptr<protoST::BytecodeModule>> g_modules;

// protoST is single-runtime-per-process (STATUS.md D2): symbols are interned
// per ProtoSpace, so creating a second STRuntime in the same process leaves
// process-wide cached symbols (e.g. the `__bc_ptr__` block-metadata key)
// pointing at the first space — which makes block dispatch in a later runtime
// fail to find `__bc_ptr__`. The whole T3-c surface therefore shares ONE
// STRuntime, constructed once via a function-local static and reused across
// every Catch2 SECTION re-run of the TEST_CASE body.
protoST::STRuntime& sharedRuntime() {
    static protoST::STRuntime rt;
    return rt;
}

// Compile `src` and run it at top level, returning the module's result.
const proto::ProtoObject* runSrc(protoST::STRuntime& rt, const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    g_modules.push_back(std::move(bc));
    return rt.runTopLevel(*g_modules.back());
}

} // namespace

TEST_CASE("T3-c: on-the-fly behaviour composition via addBehavior:",
          "[object-model][track3][t3c]") {
    protoST::STRuntime& rt = sharedRuntime();
    auto* ctx = rt.rootCtx();

    SECTION("addBehavior: adds a mixin method; a later instance responds") {
        auto* r = runSrc(rt,
            "Object subclass: #Greeter."
            "Object subclass: #Loud."
            "Loud >> shout  ^ 'HEY!'."
            "Greeter addBehavior: Loud."
            "(Greeter newChild) shout.");
        REQUIRE(r != nullptr);
        auto* s = r->asString(ctx);
        REQUIRE(s != nullptr);
        REQUIRE(s->toStdString(ctx) == "HEY!");
    }

    SECTION("the class object itself responds to the mixin method") {
        auto* r = runSrc(rt,
            "Object subclass: #ClsResp."
            "Object subclass: #Tag."
            "Tag >> tag  ^ 'tagged'."
            "ClsResp addBehavior: Tag."
            "ClsResp tag.");
        REQUIRE(r != nullptr);
        auto* s = r->asString(ctx);
        REQUIRE(s != nullptr);
        REQUIRE(s->toStdString(ctx) == "tagged");
    }

    SECTION("the class keeps its own methods and its super path") {
        // `Pup` overrides `sound` calling `super sound` on `Beast`; after
        // `addBehavior:` it must still answer `sound` correctly AND gain
        // `wag` from the mixin.
        auto* r = runSrc(rt,
            "Object subclass: #Beast."
            "Beast >> sound  ^ 1."
            "Beast subclass: #Pup."
            "Pup >> sound  ^ 10 + super sound."
            "Object subclass: #Wag."
            "Wag >> wag  ^ 100."
            "Pup addBehavior: Wag."
            "(Pup newChild) sound + (Pup newChild) wag.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 111);
    }

    SECTION("addBehavior: composes with a uses: class") {
        auto* r = runSrc(rt,
            "Object subclass: #UA."
            "UA >> a  ^ 1."
            "Object subclass: #UB."
            "UB >> b  ^ 2."
            "Object subclass: #UC uses: { UA. UB }."
            "UC >> c  ^ 4."
            "Object subclass: #UD."
            "UD >> d  ^ 8."
            "UC addBehavior: UD."
            "(UC newChild) a + (UC newChild) b "
            "  + (UC newChild) c + (UC newChild) d.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 15);
    }

    SECTION("future-instances-only — a pre-existing instance is unaffected") {
        // `pre` is created before `addBehavior:`; it must NOT gain `mm`.
        // The send is guarded so the documented limitation is pinned as a
        // caught MessageNotUnderstood, not a crash.
        auto* r = runSrc(rt,
            "Object subclass: #FG."
            "Object subclass: #FM."
            "FM >> mm  ^ 'mixin'."
            "pre := FG newChild."
            "FG addBehavior: FM."
            "[ pre mm ] on: Error do: [ :e | e messageText ].");
        REQUIRE(r != nullptr);
        auto* s = r->asString(ctx);
        REQUIRE(s != nullptr);
        // 2026-05-24 ergonomics: messageText is enriched with receiver class.
        REQUIRE(s->toStdString(ctx) == "doesNotUnderstand: mm (receiver class: FG)");
    }

    SECTION("an instance created after the call IS affected (same scenario)") {
        auto* r = runSrc(rt,
            "Object subclass: #FG2."
            "Object subclass: #FM2."
            "FM2 >> mm  ^ 'mixin'."
            "FG2 addBehavior: FM2."
            "(FG2 newChild) mm.");
        REQUIRE(r != nullptr);
        auto* s = r->asString(ctx);
        REQUIRE(s != nullptr);
        REQUIRE(s->toStdString(ctx) == "mixin");
    }

    SECTION("addParent: is a lower-level alias for addBehavior:") {
        auto* r = runSrc(rt,
            "Object subclass: #AliasCls."
            "Object subclass: #AliasMix."
            "AliasMix >> ping  ^ 77."
            "AliasCls addParent: AliasMix."
            "(AliasCls newChild) ping.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 77);
    }

    SECTION("regression: single inheritance and uses: are unaffected") {
        auto* r = runSrc(rt,
            "Object subclass: #RegBase."
            "RegBase >> base  ^ 3."
            "RegBase subclass: #RegSub."
            "RegSub >> base  ^ 1 + super base."
            "Object subclass: #RegMix."
            "RegMix >> mix  ^ 5."
            "Object subclass: #RegUser uses: { RegMix }."
            "(RegSub new base) + (RegUser new mix).");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 9);
    }
}
