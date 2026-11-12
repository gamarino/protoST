// A primitive is a protoCore native method (ProtoContext::fromMethod) bound
// under its selector on a prototype (0.10.0). Before 0.10.0 a primitive was an
// integer attribute -- the registry index tagged with bit 62 -- so any value
// attribute holding such an integer was dispatched as a primitive. These tests
// pin the two halves of the new representation: an integer is always a value,
// and reflection recognises a primitive by its being a method.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace {

const proto::ProtoObject* runSrc(protoST::STRuntime& rt, const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return rt.runTopLevel(*bc);
}

std::string asText(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    REQUIRE(v != nullptr);
    const proto::ProtoString* s = v->asString(ctx);
    REQUIRE(s != nullptr);
    return s->toStdString(ctx);
}

// 2^62 + 0: under the old encoding, the marker of the first registered
// primitive (Number>>+).
constexpr long long kOldMarkerShape = (1LL << 62) + 0;

// Declares a class `Probe` whose prototype holds, as ordinary values, the
// integer 2^62 + 0 under `big` and `big:`. Its instances inherit both.
void declareProbe(protoST::STRuntime& rt) {
    runSrc(rt, "Object subclass: #Probe.");
    auto* ctx = rt.rootCtx();
    const proto::ProtoObject* probe =
        rt.globals()->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Probe"));
    REQUIRE(probe != nullptr);
    REQUIRE(probe != PROTO_NONE);
    const proto::ProtoObject* big = ctx->fromLong(kOldMarkerShape);
    probe->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "big"), big);
    probe->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "big:"), big);
}

} // namespace

TEST_CASE("An integer attribute shaped like the old primitive marker is a value",
          "[primitive][dispatch]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();
    declareProbe(rt);

    SECTION("a unary send reads it as the value it is") {
        auto* r = runSrc(rt, "Probe new big.");
        REQUIRE(r != nullptr);
        REQUIRE(r->isInteger(ctx));
        REQUIRE(r->asLong(ctx) == kOldMarkerShape);
    }

    SECTION("a keyword send is refused: a value is not a method") {
        auto* r = runSrc(rt,
            "[Probe new big: 3. #dispatched] on: Error do: [:e | e messageText].");
        const std::string text = asText(ctx, r);
        REQUIRE(text.find("the attribute is a value, not a method") != std::string::npos);
    }

    SECTION("reflection does not take it for a method") {
        REQUIRE(runSrc(rt, "Probe new respondsTo: #big.") == PROTO_FALSE);
        REQUIRE(runSrc(rt, "Probe new respondsTo: #big:.") == PROTO_FALSE);
        REQUIRE(runSrc(rt, "Probe canUnderstand: #big.") == PROTO_FALSE);
        REQUIRE(runSrc(rt, "(Probe lookupSelector: #big) isNil.") == PROTO_TRUE);
    }

    SECTION("perform: does not dispatch it") {
        auto* r = runSrc(rt,
            "[Probe new perform: #big: with: 3. #dispatched] "
            "on: MessageNotUnderstood do: [:e | #notUnderstood].");
        REQUIRE(asText(ctx, r) == "notUnderstood");
    }
}

TEST_CASE("Reflection recognises primitives", "[primitive][reflection]") {
    protoST::STRuntime rt;
    auto* ctx = rt.rootCtx();

    SECTION("respondsTo: and canUnderstand: answer true for a primitive") {
        REQUIRE(runSrc(rt, "3 respondsTo: #+.") == PROTO_TRUE);
        REQUIRE(runSrc(rt, "#(1 2) respondsTo: #at:.") == PROTO_TRUE);
        REQUIRE(runSrc(rt, "SmallInteger canUnderstand: #+.") == PROTO_TRUE);
        REQUIRE(runSrc(rt, "3 respondsTo: #noSuchSelectorHere.") == PROTO_FALSE);
    }

    SECTION("lookupSelector: answers the class that holds the primitive") {
        auto* r = runSrc(rt, "(SmallInteger lookupSelector: #+) printString.");
        REQUIRE(asText(ctx, r) == "Number");
    }

    SECTION("selectors lists a class's primitives") {
        REQUIRE(runSrc(rt, "(Number selectors includes: #+).") == PROTO_TRUE);
        // A value attribute is not listed.
        declareProbe(rt);
        REQUIRE(runSrc(rt, "(Probe selectors includes: #big).") == PROTO_FALSE);
    }

    SECTION("perform: runs a primitive") {
        auto* r = runSrc(rt, "3 perform: #+ with: 4.");
        REQUIRE(r != nullptr);
        REQUIRE(r->asLong(ctx) == 7);
        auto* w = runSrc(rt, "3 perform: #+ withArguments: #(5).");
        REQUIRE(w->asLong(ctx) == 8);
    }

    SECTION("a primitive is never exposed as a value") {
        // A primitive is reached only by sending its selector; a unary send of
        // a primitive selector runs the primitive.
        auto* r = runSrc(rt, "#(1 2 3) size.");
        REQUIRE(r != nullptr);
        REQUIRE(r->isInteger(ctx));
        REQUIRE(r->asLong(ctx) == 3);
    }
}

TEST_CASE("A space hosts one protoST runtime, whose primitives find it by the space",
          "[primitive][runtime]") {
    // A primitive is a protoCore method: it receives only the context and
    // finds its runtime by the context's space (protoST::runtimeOf). A second
    // runtime in the same space would make that lookup ambiguous, so it is
    // refused; once the first is gone, the space takes a new one.
    proto::ProtoSpace space;
    {
        protoST::STRuntime first(space);
        REQUIRE(&protoST::runtimeOf(first.rootCtx()) == &first);
        REQUIRE_THROWS_AS(protoST::STRuntime(space), std::logic_error);
        // The refused construction left the first runtime registered.
        REQUIRE(&protoST::runtimeOf(first.rootCtx()) == &first);
        REQUIRE(runSrc(first, "3 + 4.")->asLong(first.rootCtx()) == 7);
    }
    protoST::STRuntime second(space);
    REQUIRE(&protoST::runtimeOf(second.rootCtx()) == &second);
    REQUIRE(runSrc(second, "#(1 2 3) at: 2.")->asLong(second.rootCtx()) == 2);
}

TEST_CASE("Runtimes in separate spaces each run their own primitives",
          "[primitive][runtime]") {
    protoST::STRuntime a;
    protoST::STRuntime b;
    REQUIRE(&protoST::runtimeOf(a.rootCtx()) == &a);
    REQUIRE(&protoST::runtimeOf(b.rootCtx()) == &b);
    // Alternating between the two on one thread exercises the per-thread
    // cache: each lookup answers the runtime of the context's own space.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(&protoST::runtimeOf(a.rootCtx()) == &a);
        REQUIRE(&protoST::runtimeOf(b.rootCtx()) == &b);
    }
    REQUIRE(runSrc(a, "#(5 6) at: 2.")->asLong(a.rootCtx()) == 6);
    REQUIRE(runSrc(b, "#(5 6) at: 1.")->asLong(b.rootCtx()) == 5);
    REQUIRE(runSrc(a, "3 + 4.")->asLong(a.rootCtx()) == 7);
}
