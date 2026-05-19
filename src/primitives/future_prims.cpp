#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace protoST {

// Defined in block_prims.cpp — runs a BlockClosure with the given arg vector.
extern const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* block,
                                              const proto::ProtoObject* const* args,
                                              int argc);

namespace {

// F6-A5: Future synchronisation primitives.
//
// A Future is a mutable child of futureProto with five attributes:
//   __state__     : SmallInteger — 0 = pending, 1 = resolved, 2 = rejected
//   __value__     : the resolved value (only meaningful when state == 1)
//   __error__     : the rejection cause (only meaningful when state == 2)
//   __then_cbs__  : ProtoList of BlockClosure callbacks for thenDo: (F6-A6)
//   __catch_cbs__ : ProtoList of BlockClosure callbacks for catch:   (F6-A6)
//
// These primitives are bound on futureProto. Because Future is *not* an actor
// (it lacks __wrapped__), SEND dispatch goes through the normal prototype-chain
// lookup, hits the tagged primitive marker on futureProto, and invokes the
// corresponding C++ function below.

// F6-A6: fire each registered callback with `arg`. Callback errors are
// swallowed: a misbehaving thenDo:/catch: handler must not poison the
// resolution path or starve other registered callbacks.
static void fireCallbacks(STRuntime& rt, proto::ProtoContext* ctx,
                          const proto::ProtoObject* future,
                          const proto::ProtoString* cbsKey,
                          const proto::ProtoObject* arg) {
    auto* cbsObj = future->getAttribute(ctx, cbsKey);
    if (!cbsObj || cbsObj == PROTO_NONE) return;
    auto* cbs = cbsObj->asList(ctx);
    if (!cbs) return;
    long long n = cbs->getSize(ctx);
    const proto::ProtoObject* args[] = { arg ? arg : PROTO_NONE };
    for (long long i = 0; i < n; ++i) {
        auto* cb = cbs->getAt(ctx, static_cast<int>(i));
        try { invokeBlock(rt, ctx, cb, args, 1); }
        catch (...) { /* swallow callback errors for F6 v1 */ }
    }
}

// F6-A6: register a callback block on the list stored at `cbsKey`. If the
// list slot is missing or nil, allocate a fresh empty list. Always rewrites
// the slot with the appended list (structural sharing makes appendLast COW).
static void registerCallback(proto::ProtoContext* ctx,
                             const proto::ProtoObject* future,
                             const proto::ProtoString* cbsKey,
                             const proto::ProtoObject* block) {
    auto* existing = future->getAttribute(ctx, cbsKey);
    const proto::ProtoList* list = nullptr;
    if (existing && existing != PROTO_NONE) {
        list = existing->asList(ctx);
    }
    if (!list) list = ctx->newList();
    auto* newList = list->appendLast(ctx, block);
    const_cast<proto::ProtoObject*>(future)
        ->setAttribute(ctx, cbsKey, newList->asObject(ctx));
}

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

    // F6 v2 T2: with a parallel worker draining the same queue, an empty
    // queue does NOT immediately imply deadlock — the worker may have just
    // popped the actor and be in the middle of processing. We give it one
    // short bounded wait on the scheduler cv before declaring deadlock. The
    // worker notifies schedCv on every drainOne exit (incl. future
    // resolution), so this returns promptly when progress happens. If we
    // wake up and the queue is still empty AND the future is still pending,
    // that's a true deadlock.
    int emptyStrikes = 0;
    constexpr int kMaxEmptyStrikes = 50;          // 50 × 10ms = 500ms total
    constexpr unsigned kPerWaitMillis = 10;
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
        // Pending: try to pull the next message off the scheduler.
        if (rt.drainOne(ctx)) {
            emptyStrikes = 0;  // progress: reset the deadlock budget
            continue;
        }
        // Queue empty. Either the worker has the actor in flight, or there
        // truly is no scheduled work. Wait briefly for either case.
        rt.waitForSchedulerProgress(kPerWaitMillis);
        if (++emptyStrikes >= kMaxEmptyStrikes) {
            throw std::runtime_error(
                "Future wait: pending and scheduler empty (deadlock)");
        }
    }
}

// Future>>resolve: value
//
// Idempotent: transitions a pending future to the resolved state with the
// given value. On an already-settled future, returns the receiver unchanged.
// On a successful pending→resolved transition, fires any registered thenDo:
// callbacks (F6-A6).
const proto::ProtoObject* prim_Future_resolve(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    if (argc != 1) throw std::runtime_error("Future>>resolve: expects 1 arg");
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* valueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    static const proto::ProtoString* thenCbsKey =
        ctx->fromUTF8String("__then_cbs__")->asString(ctx);
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s != 0) return r;  // already settled — no-op
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, stateKey, ctx->fromLong(1));
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, valueKey, a[0]);
    fireCallbacks(rt, ctx, r, thenCbsKey, a[0]);
    return r;
}

// Future>>rejectWith: error
//
// Idempotent: transitions a pending future to the rejected state with the
// given error. On an already-settled future, returns the receiver unchanged.
// On a successful pending→rejected transition, fires any registered catch:
// callbacks (F6-A6).
const proto::ProtoObject* prim_Future_rejectWith(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 1) throw std::runtime_error("Future>>rejectWith: expects 1 arg");
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* errorKey =
        ctx->fromUTF8String("__error__")->asString(ctx);
    static const proto::ProtoString* catchCbsKey =
        ctx->fromUTF8String("__catch_cbs__")->asString(ctx);
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s != 0) return r;  // already settled — no-op
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, stateKey, ctx->fromLong(2));
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, errorKey, a[0]);
    fireCallbacks(rt, ctx, r, catchCbsKey, a[0]);
    return r;
}

// Future>>thenDo: aBlock
//
// Registers `aBlock` to be invoked with the resolved value once the future
// resolves. If the future is already resolved, fires the block synchronously
// with the current value. If already rejected, does nothing (catch: covers
// that path). Returns the receiver so chains can continue.
const proto::ProtoObject* prim_Future_thenDo(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const* a,
                                              int argc) {
    if (argc != 1) throw std::runtime_error("Future>>thenDo: expects 1 arg");
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* valueKey =
        ctx->fromUTF8String("__value__")->asString(ctx);
    static const proto::ProtoString* thenCbsKey =
        ctx->fromUTF8String("__then_cbs__")->asString(ctx);
    auto* block = a[0];
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s == 1) {
        // Already resolved — fire callback immediately with __value__.
        auto* v = r->getAttribute(ctx, valueKey);
        const proto::ProtoObject* args[] = { v ? v : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (...) { /* swallow */ }
    } else if (s == 0) {
        // Pending — register for later firing on resolve.
        registerCallback(ctx, r, thenCbsKey, block);
    }
    // s == 2 (already rejected): thenDo callbacks never fire on the
    // rejection path; drop silently.
    return r;
}

// Future>>catch: aBlock
//
// Registers `aBlock` to be invoked with the error value once the future is
// rejected. If the future is already rejected, fires the block synchronously
// with the current error. If already resolved, does nothing. Returns the
// receiver so chains can continue.
const proto::ProtoObject* prim_Future_catch(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a,
                                             int argc) {
    if (argc != 1) throw std::runtime_error("Future>>catch: expects 1 arg");
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    static const proto::ProtoString* errorKey =
        ctx->fromUTF8String("__error__")->asString(ctx);
    static const proto::ProtoString* catchCbsKey =
        ctx->fromUTF8String("__catch_cbs__")->asString(ctx);
    auto* block = a[0];
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s == 2) {
        // Already rejected — fire callback immediately with __error__.
        auto* e = r->getAttribute(ctx, errorKey);
        const proto::ProtoObject* args[] = { e ? e : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (...) { /* swallow */ }
    } else if (s == 0) {
        // Pending — register for later firing on reject.
        registerCallback(ctx, r, catchCbsKey, block);
    }
    // s == 1 (already resolved): catch callbacks never fire on the
    // resolved path; drop silently.
    return r;
}

} // anon

// Exposed for STRuntime::drainOne to fire callbacks at the resolve/reject
// transition points without re-implementing the iteration loop.
void fireFutureThenCallbacks(STRuntime& rt, proto::ProtoContext* ctx,
                              const proto::ProtoObject* future,
                              const proto::ProtoObject* value) {
    static const proto::ProtoString* thenCbsKey =
        ctx->fromUTF8String("__then_cbs__")->asString(ctx);
    fireCallbacks(rt, ctx, future, thenCbsKey, value);
}

void fireFutureCatchCallbacks(STRuntime& rt, proto::ProtoContext* ctx,
                               const proto::ProtoObject* future,
                               const proto::ProtoObject* error) {
    static const proto::ProtoString* catchCbsKey =
        ctx->fromUTF8String("__catch_cbs__")->asString(ctx);
    fireCallbacks(rt, ctx, future, catchCbsKey, error);
}

void installFuturePrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.futureProto, "wait",
                  reg.registerPrim(prim_Future_wait));
    bindPrimitive(rt, b.futureProto, "resolve:",
                  reg.registerPrim(prim_Future_resolve));
    bindPrimitive(rt, b.futureProto, "rejectWith:",
                  reg.registerPrim(prim_Future_rejectWith));
    bindPrimitive(rt, b.futureProto, "thenDo:",
                  reg.registerPrim(prim_Future_thenDo));
    bindPrimitive(rt, b.futureProto, "catch:",
                  reg.registerPrim(prim_Future_catch));
}

} // namespace protoST
