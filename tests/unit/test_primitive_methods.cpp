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

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

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

namespace {

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* s) {
    return proto::ProtoString::createSymbol(ctx, s);
}

// A protoCore call of `selector` on `target`: what another runtime (or
// embedder C++) does when it calls a protoST object's method cell directly.
const proto::ProtoObject* callDirect(proto::ProtoContext* ctx, const proto::ProtoObject* target,
                                     const char* selector,
                                     std::initializer_list<const proto::ProtoObject*> args = {}) {
    std::vector<const proto::ProtoObject*> a(args);
    return target->call(ctx, nullptr, sym(ctx, selector), target, a.data(),
                        static_cast<unsigned>(a.size()));
}

// Foreign methods protoST calls, which call a primitive back directly while
// protoST's dispatch is active on the thread.
const proto::ProtoObject* printBack(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                    const proto::ParentLink*,
                                    const proto::ProtoArguments* positional,
                                    const proto::ProtoKeywordArguments*) {
    return callDirect(ctx, positional->getAt(ctx, 0), "printString");
}

const proto::ProtoObject* valueBack(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                    const proto::ParentLink*,
                                    const proto::ProtoArguments* positional,
                                    const proto::ProtoKeywordArguments*) {
    return callDirect(ctx, positional->getAt(ctx, 0), "value");
}

// protoST's number prototype: a SmallInteger names no prototype of its own
// for protoCore's `call`, so the method cell is called on this holder.
const proto::ProtoObject* g_numberProto = nullptr;

const proto::ProtoObject* divideBack(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                     const proto::ParentLink*,
                                     const proto::ProtoArguments* positional,
                                     const proto::ProtoKeywordArguments*) {
    const proto::ProtoObject* args[] = {ctx->fromLong(0)};
    return g_numberProto->call(ctx, nullptr, sym(ctx, "/"), positional->getAt(ctx, 0), args, 1);
}

} // namespace

// A primitive is a protoCore method cell, so any caller may call it through
// protoCore's calling convention. A call from outside protoST's dispatch runs
// behind the boundary __st_send__ gives (docs/INTEROP.md section 4.3).
TEST_CASE("A primitive called through protoCore runs behind protoST's boundary",
          "[primitive][interop]") {
    SECTION("embedder C++ calls the method cell on protoST's own context") {
        protoST::STRuntime rt;
        auto* ctx = rt.rootCtx();
        const proto::ProtoObject* three = ctx->fromLong(3);
        // The integer's primitives live on protoST's number prototype.
        const proto::ProtoObject* holder = rt.bootstrap().numberProto;
        const proto::ProtoObject* const* none = nullptr;
        REQUIRE(asText(ctx, holder->call(ctx, nullptr, sym(ctx, "printString"), three, none,
                                         0)) == "3");
        // protoST's own dispatch is unaffected afterwards.
        REQUIRE(asText(ctx, runSrc(rt, "4 printString.")) == "4");
    }

    SECTION("another runtime sharing protoST's space calls it, and errors leave as exceptions") {
        proto::ProtoSpace space;
        protoST::STRuntime rt(space);
        // The host's own context in the shared space: not one of protoST's.
        proto::ProtoContext host(&space, nullptr);
        runSrc(rt, "Object subclass: #Shared.");
        const proto::ProtoObject* cls =
            rt.globals()->getAttribute(rt.rootCtx(), sym(rt.rootCtx(), "Shared"));
        const proto::ProtoObject* inst = runSrc(rt, "Shared new.");
        // An instance finds `class`, a primitive, through its own chain.
        REQUIRE(inst->getAttribute(&host, sym(&host, "class"))->isMethod(&host));
        REQUIRE(callDirect(&host, inst, "class") == cls);
        // A failure inside the primitive leaves as a protoCore exception, as it
        // does through __st_send__.
        // (A SmallInteger names no prototype of its own for protoCore's
        // `call`, so the call names protoST's number prototype.)
        bool caught = false;
        try {
            const proto::ProtoObject* args[] = {host.fromLong(0)};
            rt.bootstrap().numberProto->call(&host, nullptr, sym(&host, "/"),
                                             host.fromLong(3), args, 1);
        } catch (const proto::ProtoException&) {
            caught = true;
        }
        REQUIRE(caught);
    }

    SECTION("a foreign method protoST calls can call a primitive back") {
        protoST::STRuntime rt;
        auto* ctx = rt.rootCtx();
        auto* foreign = rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/true);
        foreign->setAttribute(ctx, sym(ctx, "print:"), ctx->fromMethod(nullptr, &printBack));
        foreign->setAttribute(ctx, sym(ctx, "value:"), ctx->fromMethod(nullptr, &valueBack));
        foreign->setAttribute(ctx, sym(ctx, "divide:"), ctx->fromMethod(nullptr, &divideBack));
        rt.globals()->setAttribute(ctx, sym(ctx, "Foreign"), foreign);
        g_numberProto = rt.bootstrap().numberProto;
        REQUIRE(asText(ctx, runSrc(rt, "Foreign print: 3.")) == "3");
        REQUIRE(runSrc(rt, "Foreign value: [41 + 1].")->asLong(ctx) == 42);
        // The error crosses the foreign frames and is signalled again where
        // the foreign method returns into protoST, so the outer handler
        // selects it by its class.
        REQUIRE(asText(ctx, runSrc(rt,
            "[Foreign divide: 3. #none] on: ZeroDivide do: [:e | #caught].")) == "caught");
        // The boundary: a non-local return cannot unwind through the foreign
        // method's frames; without it the method would answer 5.
        runSrc(rt, "Object subclass: #Holder.");
        auto* r = runSrc(rt,
            "Holder >> run ^ Foreign value: [^ 5].\n"
            "[Holder new run] on: Error do: [:e | e messageText].");
        REQUIRE(asText(ctx, r).find("cannot cross a runtime boundary") != std::string::npos);
        REQUIRE(asText(ctx, runSrc(rt, "3 printString.")) == "3");
    }
}
