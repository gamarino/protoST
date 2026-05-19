#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace protoST {

namespace {

// F6-A5: Future synchronisation primitives.
//
// A Future is a mutable child of futureProto with three attributes:
//   __state__ : SmallInteger — 0 = pending, 1 = resolved, 2 = rejected
//   __value__ : the resolved value (only meaningful when state == 1)
//   __error__ : the rejection cause (only meaningful when state == 2)
//
// These primitives are bound on futureProto. Because Future is *not* an actor
// (it lacks __wrapped__), SEND dispatch goes through the normal prototype-chain
// lookup, hits the tagged primitive marker on futureProto, and invokes the
// corresponding C++ function below.

// Future>>wait
//
// Drains the scheduler's ready queue until self's state transitions out of
// pending. On a resolved future, returns __value__. On a rejected future,
// throws std::runtime_error carrying the rejection cause. If the queue empties
// while the future is still pending, throws a deadlock error.
const proto::ProtoObject* prim_Future_wait(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* valueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    static const proto::ProtoString* errorKey =
        ctx->fromUTF8String("__error__")->asString(ctx);

    while (true) {
        auto* st = r->getAttribute(ctx, stateKey);
        long long s = st ? st->asLong(ctx) : 0;
        if (s == 1) {
            auto* v = r->getAttribute(ctx, valueKey);
            return v ? v : PROTO_NONE;
        }
        if (s == 2) {
            auto* e = r->getAttribute(ctx, errorKey);
            std::string msg = (e && e != PROTO_NONE)
                ? e->asString(ctx)->toStdString(ctx)
                : std::string("rejected");
            throw std::runtime_error("Future rejected: " + msg);
        }
        // Pending: pull the next message off the scheduler.
        if (!rt.drainOne(ctx)) {
            throw std::runtime_error(
                "Future wait: pending and scheduler empty (deadlock)");
        }
    }
}

// Future>>resolve: value
//
// Idempotent: transitions a pending future to the resolved state with the
// given value. On an already-settled future, returns the receiver unchanged.
// Callback firing lands in F6-A6.
const proto::ProtoObject* prim_Future_resolve(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    if (argc != 1) throw std::runtime_error("Future>>resolve: expects 1 arg");
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* valueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s != 0) return r;  // already settled — no-op
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, stateKey, ctx->fromLong(1));
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, valueKey, a[0]);
    return r;
}

// Future>>rejectWith: error
//
// Idempotent: transitions a pending future to the rejected state with the
// given error. On an already-settled future, returns the receiver unchanged.
const proto::ProtoObject* prim_Future_rejectWith(STRuntime&, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 1) throw std::runtime_error("Future>>rejectWith: expects 1 arg");
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* errorKey =
        ctx->fromUTF8String("__error__")->asString(ctx);
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s != 0) return r;  // already settled — no-op
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, stateKey, ctx->fromLong(2));
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, errorKey, a[0]);
    return r;
}

} // anon

void installFuturePrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.futureProto, "wait",
                  reg.registerPrim(prim_Future_wait));
    bindPrimitive(rt, b.futureProto, "resolve:",
                  reg.registerPrim(prim_Future_resolve));
    bindPrimitive(rt, b.futureProto, "rejectWith:",
                  reg.registerPrim(prim_Future_rejectWith));
}

} // namespace protoST
