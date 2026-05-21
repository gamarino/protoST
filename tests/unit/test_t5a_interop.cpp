// Track 5, sub-slice T5-a — consumer-side cross-language UMD interop.
//
// protoJS and protoPython are not present in this repository, so the
// cross-provider consumption path is exercised with a *stand-in* foreign UMD
// provider — a small `proto::ModuleProvider` registered with protoCore's
// global `ProviderRegistry` only inside this test translation unit (the
// production runtime is never polluted with a fake provider). The provider's
// `tryLoad` hands back a `ProtoObject` deliberately built to resemble a module
// from another protoCore runtime: a "class"/object carrying methods
// (primitive-backed and attribute-stored), some state, and immediates.
//
// What is verified — the protoST *consumer* side:
//   * `Import from: '<foreign-module>'` resolves THROUGH the stand-in
//     provider once its spec is added to protoST's resolution chain, and
//     returns the foreign `ProtoObject`;
//   * protoST message sends — unary and keyword — dispatch to a
//     foreign-provided object (its methods are `ProtoObject` attributes, so
//     protoST's `getAttribute`-based SEND finds them): a primitive-backed
//     foreign method AND an attribute-stored value;
//   * a foreign object carried through a protoST collection and through a
//     block behaves as an ordinary `ProtoObject`;
//   * immediates handed back by the foreign side (42, true, a String) are
//     used directly in protoST arithmetic / comparison with NO conversion;
//   * a foreign collection-like wrapper (distinct from a protoST `Array`) is
//     consumed by sending it the foreign protocol — the impedance point that
//     `docs/INTEROP.md` documents.
//
// See `docs/INTEROP.md` for the full strategy this slice underpins.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

#include <atomic>
#include <string>

namespace {

// --- The stand-in foreign runtime -------------------------------------------
//
// A real foreign runtime (protoJS / protoPython) builds its objects from the
// same 64-byte protoCore cells protoST uses, so a "foreign" object IS an
// ordinary `proto::ProtoObject`. The only thing this stand-in shortcuts is
// where the object is *constructed*: it borrows protoST's `STRuntime` so the
// one genuinely runtime-specific piece — a primitive-backed method — can be
// registered. In a live tri-runtime host each runtime registers its own
// primitives; here the test harness plays that coordinating host (see
// `docs/INTEROP.md` §"The cross-repo follow-up").

// A foreign primitive-backed method: `widget doubleIt: n` → n * 2.
// Signature matches protoST's `PrimFn`; it is registered in the shared
// primitive registry exactly as a protoST primitive would be.
const proto::ProtoObject* fakeForeignDoubleIt(protoST::STRuntime&,
                                              proto::ProtoContext* ctx,
                                              const proto::ProtoObject* /*recv*/,
                                              const proto::ProtoObject* const* a,
                                              int argc) {
    if (argc != 1) throw std::runtime_error("doubleIt: expects 1 arg");
    long long n = a[0]->asLong(ctx);
    return ctx->fromLong(n * 2);
}

// Builds the foreign module object. The module is a plain mutable ProtoObject
// whose attributes are the things a protoST `Import` consumer would reach for:
//   * `Widget`         — a foreign "class"/object with methods + state;
//   * `answerInt/Bool/String` — bare immediates, shared cell types;
//   * `Bag`            — a foreign collection-like wrapper, NOT a protoST Array.
const proto::ProtoObject* buildForeignModule(protoST::STRuntime& rt,
                                             proto::ProtoContext* ctx) {
    auto& b = rt.bootstrap();
    auto* objectProto = b.objectProto;

    // The foreign "class"/object: a mutable child of objectProto carrying
    // both a primitive-backed method and an attribute-stored value.
    auto* widget = objectProto->newChild(ctx, /*isMutable=*/true);

    // Primitive-backed foreign method `doubleIt:` — registered in the shared
    // registry and bound on the foreign object as a tagged-int marker, the
    // same representation protoST's own primitives use, so protoST's SEND
    // dispatch invokes it with no special-casing.
    int primIdx = rt.registry().registerPrim(fakeForeignDoubleIt);
    protoST::bindPrimitive(rt, widget, "doubleIt:", primIdx);

    // Attribute-stored foreign value `label` — a unary send reads it back as
    // an ordinary property (protoST treats a non-primitive value attribute
    // reached by a unary send as the result).
    auto* labelKey = proto::ProtoString::createSymbol(ctx, "label");
    widget->setAttribute(ctx, labelKey,
                         ctx->fromUTF8String("foreign-widget"));

    // Foreign state — a bare immediate stored as an attribute.
    auto* stateKey = proto::ProtoString::createSymbol(ctx, "version");
    widget->setAttribute(ctx, stateKey, ctx->fromLong(7));

    // The foreign collection-like wrapper. It is NOT a protoST Array (no
    // `__data__` attribute, no Array prototype) — it is the foreign runtime's
    // own wrapper. protoST consumes it by sending it the foreign protocol
    // (`sizeOf`, `itemAt:`), which is exactly the documented impedance point.
    auto* bag = objectProto->newChild(ctx, /*isMutable=*/true);
    {
        // `sizeOf` — attribute-stored count (foreign protocol, unary).
        auto* sizeKey = proto::ProtoString::createSymbol(ctx, "sizeOf");
        bag->setAttribute(ctx, sizeKey, ctx->fromLong(3));
        // Foreign elements stored under foreign-style keys item0..item2.
        for (int i = 0; i < 3; ++i) {
            std::string k = "item" + std::to_string(i);
            auto* ik = proto::ProtoString::createSymbol(ctx, k.c_str());
            bag->setAttribute(ctx, ik, ctx->fromLong(10 + i));
        }
    }

    // Assemble the module object.
    auto* mod = objectProto->newChild(ctx, /*isMutable=*/true);
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Widget"),
                      widget);
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Bag"), bag);
    // Immediates exposed directly — shared cell types, no conversion needed.
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "answerInt"),
                      ctx->fromLong(42));
    mod->setAttribute(ctx,
                      proto::ProtoString::createSymbol(ctx, "answerBool"),
                      PROTO_TRUE);
    mod->setAttribute(ctx,
                      proto::ProtoString::createSymbol(ctx, "answerString"),
                      ctx->fromUTF8String("hello"));
    return mod;
}

// The runtime the provider should build its module against. Catch2 re-runs a
// TEST_CASE body once per SECTION with a FRESH `STRuntime`, while the provider
// is registered with the process-global ProviderRegistry exactly once. The
// provider therefore cannot capture a runtime pointer at registration time —
// it would dangle on every later section. Instead the test publishes the
// live runtime here before each `Import`, and the provider reads it.
std::atomic<protoST::STRuntime*> g_currentForeignRuntime{nullptr};

// The stand-in foreign UMD provider. Registered with protoCore's global
// ProviderRegistry; alias "fake", GUID "fake-foreign-runtime". Its tryLoad
// answers only for the logical paths this test asks for, returning PROTO_NONE
// for anything else (the protoCore ModuleProvider "not my module" contract).
class FakeForeignProvider : public proto::ModuleProvider {
public:
    FakeForeignProvider() : guid_("fake-foreign-runtime"), alias_("fake") {}

    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                      proto::ProtoContext* ctx) override {
        // Answer for any path beginning with "foreign:" — the test uses a
        // distinct suffix per section so the process-global SharedModuleCache
        // does not leak a module between sections.
        if (logicalPath.rfind("foreign:", 0) != 0) return PROTO_NONE;
        protoST::STRuntime* rt = g_currentForeignRuntime.load();
        if (!rt) return PROTO_NONE;
        return buildForeignModule(*rt, ctx);
    }
    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }

private:
    std::string guid_;
    std::string alias_;
};

// Compile `src`, run it at top level, return the module's result.
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

TEST_CASE("T5-a: protoST consumes a module from a foreign UMD provider",
          "[interop][track5][t5a]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    // Register the stand-in foreign provider with protoCore's global
    // ProviderRegistry. The registry is process-global and a provider cannot
    // be unregistered, so registration is guarded by a once-flag — every
    // TEST_CASE run in this process shares the single instance.
    static std::atomic<bool> s_registered{false};
    {
        bool expected = false;
        if (s_registered.compare_exchange_strong(expected, true)) {
            proto::ProviderRegistry::instance().registerProvider(
                std::make_unique<FakeForeignProvider>());
        }
    }
    // Publish THIS section's live runtime so the provider builds the foreign
    // module against the correct ProtoSpace (Catch2 re-runs the body, and
    // hence reconstructs `rt`, once per SECTION).
    g_currentForeignRuntime.store(&rt);

    // CONSUMER-SIDE GAP + FIX. protoST's constructor sets the resolution chain
    // to just `["provider:st"]`. protoCore's getImportModule walks that chain;
    // a `provider:` entry is looked up in the ProviderRegistry. A foreign
    // provider can be *registered* yet stay unreachable because its spec is
    // not in the chain. STRuntime::addModuleProviderToChain (added for T5-a)
    // appends the foreign spec — AFTER `provider:st`, so a same-named protoST
    // module still wins.
    rt.addModuleProviderToChain("provider:fake");

    SECTION("Import resolves through the foreign provider and returns its object") {
        const proto::ProtoObject* mod =
            runSrc(rt, "Import from: 'foreign:basic'.");
        REQUIRE(mod != nullptr);
        REQUIRE(mod != PROTO_NONE);
        // The module carries the foreign Widget class.
        auto* widgetSym = proto::ProtoString::createSymbol(ctx, "Widget");
        auto* widget = mod->getAttribute(ctx, widgetSym);
        REQUIRE(widget != nullptr);
        REQUIRE(widget != PROTO_NONE);
    }

    SECTION("keyword message dispatches to a primitive-backed foreign method") {
        // `w doubleIt: 21` dispatches by selector through getAttribute and
        // invokes the foreign primitive — 21 * 2 = 42.
        auto* r = runSrc(rt,
            "m := Import from: 'foreign:kw'."
            "w := m Widget."
            "w doubleIt: 21.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 42);
    }

    SECTION("unary message reads an attribute-stored foreign value") {
        // `w label` — a unary send to a non-primitive value attribute reads
        // the attribute itself: the foreign String "foreign-widget".
        auto* r = runSrc(rt,
            "m := Import from: 'foreign:unary'."
            "(m Widget) label.");
        REQUIRE(r != nullptr);
        auto* s = r->asString(ctx);
        REQUIRE(s != nullptr);
        REQUIRE(s->toStdString(ctx) == "foreign-widget");
    }

    SECTION("immediates from the foreign side are used with NO conversion") {
        // A foreign SmallInteger IS a protoST SmallInteger — shared cell type.
        auto* sum = runSrc(rt,
            "m := Import from: 'foreign:imm'."
            "(m answerInt) + 1.");
        REQUIRE(sum != nullptr);
        REQUIRE(sum->asLong(ctx) == 43);

        // A foreign Boolean drives a protoST conditional directly.
        auto* boolUse = runSrc(rt,
            "m := Import from: 'foreign:imm'."
            "(m answerBool) ifTrue: [ 100 ] ifFalse: [ 200 ].");
        REQUIRE(boolUse != nullptr);
        REQUIRE(boolUse->asLong(ctx) == 100);

        // A foreign String compares equal to a protoST String literal.
        auto* strEq = runSrc(rt,
            "m := Import from: 'foreign:imm'."
            "(m answerString) = 'hello'.");
        REQUIRE(strEq == PROTO_TRUE);
    }

    SECTION("a foreign object is carried through a protoST collection") {
        // Put the foreign Widget into an OrderedCollection, read it back, and
        // send it a foreign message — it behaves as an ordinary ProtoObject.
        auto* r = runSrc(rt,
            "m := Import from: 'foreign:coll'."
            "c := OrderedCollection new."
            "c add: (m Widget)."
            "(c at: 1) doubleIt: 5.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 10);
    }

    SECTION("a foreign object passes through a protoST block") {
        // A block closing over the foreign object dispatches to it normally.
        auto* r = runSrc(rt,
            "m := Import from: 'foreign:block'."
            "w := m Widget."
            "blk := [:x | x doubleIt: 8]."
            "blk value: w.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 16);
    }

    SECTION("a foreign collection wrapper is consumed via the foreign protocol") {
        // The foreign Bag is NOT a protoST Array — protoST consumes it by
        // sending it the foreign protocol (`sizeOf`, `itemN`). This is the
        // documented impedance point: an adapter would be needed to present
        // it as a protoST SequenceableCollection.
        auto* sz = runSrc(rt,
            "m := Import from: 'foreign:bag'."
            "(m Bag) sizeOf.");
        REQUIRE(sz != nullptr);
        REQUIRE(sz->asLong(ctx) == 3);

        // Reading a foreign element by its foreign-style accessor.
        auto* elem = runSrc(rt,
            "m := Import from: 'foreign:bag'."
            "(m Bag) item1.");
        REQUIRE(elem != nullptr);
        REQUIRE(elem->asLong(ctx) == 11);

        // Adapter sketch: protoST code copies the foreign elements into a
        // native Array by walking the foreign protocol. After the copy the
        // result is an ordinary protoST collection.
        auto* adapted = runSrc(rt,
            "m := Import from: 'foreign:bag'."
            "bag := m Bag."
            "arr := OrderedCollection new."
            "arr add: (bag item0)."
            "arr add: (bag item1)."
            "arr add: (bag item2)."
            "(arr at: 1) + (arr at: 2) + (arr at: 3).");
        REQUIRE(adapted != nullptr);
        REQUIRE(adapted->asLong(ctx) == 33);  // 10 + 11 + 12
    }
}

TEST_CASE("T5-a: protoST's own .st module import still resolves (regression)",
          "[interop][track5][t5a]") {
    // The foreign provider is appended AFTER `provider:st`; protoST's own
    // module system must be unaffected.
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    rt.addModuleProviderToChain("provider:fake");

    std::string path = std::string(PROTOST_FIXTURES_DIR) + "/lib_simple.st";
    auto* module = rt.loadModuleFromFile(ctx, path, "lib_simple");
    REQUIRE(module != nullptr);
    REQUIRE(module != PROTO_NONE);
    auto* greeterSym = proto::ProtoString::createSymbol(ctx, "Greeter");
    auto* greeterClass = module->getAttribute(ctx, greeterSym);
    REQUIRE(greeterClass != nullptr);
    REQUIRE(greeterClass != PROTO_NONE);
}

TEST_CASE("T5-a: addModuleProviderToChain is idempotent",
          "[interop][track5][t5a]") {
    protoST::STRuntime rt;
    // Adding the same spec twice must not duplicate the chain entry; calling
    // it must never disturb resolution of `provider:st`.
    rt.addModuleProviderToChain("provider:fake");
    rt.addModuleProviderToChain("provider:fake");
    auto* chainObj = rt.space()->getResolutionChain();
    REQUIRE(chainObj != nullptr);
    REQUIRE(chainObj != PROTO_NONE);
    auto* chain = chainObj->asList(rt.rootCtx());
    REQUIRE(chain != nullptr);
    // Count "provider:fake" occurrences — must be exactly one.
    int fakeCount = 0;
    unsigned long n = chain->getSize(rt.rootCtx());
    for (unsigned long i = 0; i < n; ++i) {
        auto* e = chain->getAt(rt.rootCtx(), static_cast<int>(i));
        auto* es = e ? e->asString(rt.rootCtx()) : nullptr;
        if (es && es->toStdString(rt.rootCtx()) == "provider:fake") ++fakeCount;
    }
    REQUIRE(fakeCount == 1);
}
