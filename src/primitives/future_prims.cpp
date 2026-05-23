#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/FutureYield.h"
#include "runtime/SchedDiag.h"
#include "runtime/GcSafeBlocking.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace protoST {

// Defined in block_prims.cpp — runs a BlockClosure with the given arg vector.
extern const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* block,
                                              const proto::ProtoObject* const* args,
                                              int argc);

// ===========================================================================
// Lock-free Future
//
// A Future is a mutable child of futureProto. It carries:
//   __state__      : SmallInteger — 0 = pending, 1 = resolved, 2 = rejected
//   __value__      : the resolved value (meaningful once __state__ == 1)
//   __error__      : the rejection cause (meaningful once __state__ == 2)
//   __then_cbs__   : ProtoList of thenDo: callbacks  / PROTO_NONE once drained
//   __catch_cbs__  : ProtoList of catch:  callbacks  / PROTO_NONE once drained
//   __waiters__    : ProtoList of yielded actors     / PROTO_NONE once drained
//   __settling__   : claim flag — absent while pending, PROTO_TRUE once a
//                    resolver has won the right to settle this future
//
// There is NO per-future mutex or condition variable. The state machine is
// lock-free, built entirely on protoCore's atomic attribute compare-and-swap
// (ProtoObject::setAttributeIfEqual):
//
//   * Settlement is claimed with a single CAS on __settling__ (absent ->
//     PROTO_TRUE). Exactly one resolver wins; every later resolve/reject is a
//     no-op. The winner then writes the value/error, fires callbacks, and
//     ONLY THEN publishes __state__ — so a thread that observes a settled
//     __state__ is guaranteed every callback registered before the settle has
//     already run ("Future>>wait returns => all thenDo:/catch: blocks done").
//
//   * thenDo:/catch:/yield register into __then_cbs__/__catch_cbs__/__waiters__
//     with a CAS-append. When a settler drains one of those lists it swaps it
//     to the terminal sentinel PROTO_NONE. A registrant therefore either
//     CAS-appends (the settler will fire/schedule it) or observes PROTO_NONE
//     (the settler already drained — the registrant fires/schedules itself).
//     The sentinel is monotonic and terminal, so this is exactly-once.
//
//   * Future>>wait polls __state__ with a GC-safe, backed-off bounded sleep.
//     The actor path throws FutureYield instead of blocking.
// ===========================================================================

namespace {

// Fire each callback in `list` with `arg`. Callback errors are swallowed: a
// misbehaving thenDo:/catch: handler must not poison the resolution path or
// starve the other callbacks.
void fireCallbackList(STRuntime& rt, proto::ProtoContext* ctx,
                      const proto::ProtoList* list,
                      const proto::ProtoObject* arg) {
    if (!list) return;
    long long n = list->getSize(ctx);
    const proto::ProtoObject* cargs[] = { arg ? arg : PROTO_NONE };
    for (long long i = 0; i < n; ++i) {
        auto* cb = list->getAt(ctx, static_cast<int>(i));
        try { invokeBlock(rt, ctx, cb, cargs, 1); }
        catch (...) { /* swallow callback errors */ }
    }
}

// Atomically take the list stored under `key` and replace it with the drained
// sentinel (PROTO_NONE). Returns the old list (nullptr if it was absent or
// empty). Called by the unique settlement winner, so it only ever contends
// with concurrent CAS-appends — never with another drainer.
const proto::ProtoList* drainList(proto::ProtoContext* ctx,
                                  const proto::ProtoObject* fut,
                                  const proto::ProtoString* key) {
    for (;;) {
        const proto::ProtoObject* cur = fut->getOwnAttributeDirect(ctx, key);
        if (cur == PROTO_NONE) return nullptr;  // already drained
        // Pin the snapshot across the CAS (setAttributeIfEqual allocates).
        TransientPin pinCur(ctx, cur);
        if (const_cast<proto::ProtoObject*>(fut)
                ->setAttributeIfEqual(ctx, key, cur, PROTO_NONE)) {
            return (cur && cur != PROTO_NONE) ? cur->asList(ctx) : nullptr;
        }
    }
}

// CAS-append `item` to the list stored under `key`. Returns true if the item
// was appended (a settler will fire/schedule it); false if the list had
// already been drained to PROTO_NONE (the future settled — the caller must
// fire/schedule `item` itself).
bool appendOrDrained(proto::ProtoContext* ctx,
                     const proto::ProtoObject* fut,
                     const proto::ProtoString* key,
                     const proto::ProtoObject* item) {
    for (;;) {
        const proto::ProtoObject* cur = fut->getOwnAttributeDirect(ctx, key);
        if (cur == PROTO_NONE) return false;  // drained — caller handles `item`
        const proto::ProtoList* list = (cur && cur != PROTO_NONE)
            ? cur->asList(ctx) : ctx->newList();
        // `cur`/`list`/`newList`/`newObj` are transients held across allocating
        // calls (appendLast, asObject, setAttributeIfEqual) — pin each.
        TransientPin pinCur(ctx, cur);
        TransientPin pinList(ctx, reinterpret_cast<const proto::ProtoObject*>(list));
        const proto::ProtoList* newList = list->appendLast(ctx, item);
        TransientPin pinNewList(
            ctx, reinterpret_cast<const proto::ProtoObject*>(newList));
        const proto::ProtoObject* newObj = newList->asObject(ctx);
        TransientPin pinNewObj(ctx, newObj);
        if (const_cast<proto::ProtoObject*>(fut)
                ->setAttributeIfEqual(ctx, key, cur, newObj)) {
            return true;
        }
    }
}

// The core lock-free settle. `reject == false` resolves with `payload` as the
// value; `reject == true` rejects with `payload` as the error. Idempotent:
// the first caller to win the __settling__ CAS settles the future; every
// later call (and a concurrent loser) is a silent no-op.
void settleFuture(STRuntime& rt, proto::ProtoContext* ctx,
                  const proto::ProtoObject* future,
                  bool reject, const proto::ProtoObject* payload) {
    if (!future) return;
    const proto::ProtoString* settlingKey =
        rt.bootstrap().sym.settling;
    // F6 v5 hot-path fix (2026-05-23): these were createSymbol calls in every
    // settleFuture (and every readState), forcing a SymbolTable shard-lock
    // lookup + UTF-8 normalisation per call. Under multi-actor load that was
    // the single biggest contention point (~9.5% of CPU in perf, the
    // serialisation bottleneck the user asked us to find). All four hot keys
    // are now read from the Bootstrap cache (perpetual interned symbols).
    const proto::ProtoString* stateKey =
        rt.bootstrap().sym.state;
    const proto::ProtoString* valueKey =
        rt.bootstrap().sym.value;
    const proto::ProtoString* errorKey =
        rt.bootstrap().sym.error;
    const proto::ProtoString* thenCbsKey =
        rt.bootstrap().sym.thenCbs;
    const proto::ProtoString* catchCbsKey =
        rt.bootstrap().sym.catchCbs;
    const proto::ProtoString* waitersKey =
        rt.bootstrap().sym.waiters;

    // `payload` is held across every step below (each allocates) — pin it.
    TransientPin pinPayload(ctx, payload ? payload : PROTO_NONE);

    // 1. Claim. setAttributeIfEqual(absent -> PROTO_TRUE) succeeds for exactly
    //    one caller; everyone else loses and returns.
    if (!const_cast<proto::ProtoObject*>(future)
            ->setAttributeIfEqual(ctx, settlingKey, nullptr, PROTO_TRUE)) {
        return;  // already settled / being settled
    }

    // 2. Write the result BEFORE publishing __state__, so any reader that
    //    later observes a settled state always sees a consistent value/error.
    const_cast<proto::ProtoObject*>(future)->setAttribute(
        ctx, reject ? errorKey : valueKey, payload ? payload : PROTO_NONE);

    // 3. Drain and fire the matching callbacks (thenDo: on resolve, catch: on
    //    reject). A concurrent thenDo:/catch: either appended before this
    //    drain (its callback is in the snapshot we fire) or sees the drained
    //    sentinel and fires itself — exactly once.
    const proto::ProtoList* cbs =
        drainList(ctx, future, reject ? catchCbsKey : thenCbsKey);
    if (cbs) {
        TransientPin pinCbs(ctx, reinterpret_cast<const proto::ProtoObject*>(cbs));
        fireCallbackList(rt, ctx, cbs, payload);
    }

    // 4. Publish the final state. A Future>>wait polling __state__ observes
    //    the settled value only now — after every pre-settle callback ran.
    const_cast<proto::ProtoObject*>(future)->setAttribute(
        ctx, stateKey, ctx->fromLong(reject ? 2 : 1));

    // 4b. Wake the main thread iff it is parked specifically on THIS future.
    //     Cheap fast path inside notifyMainWaiterIfFor: one atomic load of
    //     `mainWaitingOn`; if the value doesn't match `future`, return
    //     immediately (zero-cost for any unrelated settle). The seq_cst
    //     pair between `markMainWaitingOn` and the `__state__` store above
    //     makes the wait race-free: either the waiter sees the settled
    //     state and never parks, or it parks AFTER setting `mainWaitingOn`
    //     and the load below observes the pointer match.
    rt.notifyMainWaiterIfFor(future);

    // 5. Drain and reschedule yielded actor waiters. Done after __state__ is
    //    published so a resumed actor reads the final state. A yield racing
    //    this drain either appended first (we schedule it) or sees the
    //    sentinel and is scheduled by the engine's FutureYield path.
    const proto::ProtoList* waiters = drainList(ctx, future, waitersKey);
    if (waiters) {
        TransientPin pinWaiters(
            ctx, reinterpret_cast<const proto::ProtoObject*>(waiters));
        long long n = waiters->getSize(ctx);
        SCHED_DIAG("settleFuture future=" << future << " reject=" << reject
                   << " waiters=" << n);
        for (long long i = 0; i < n; ++i) {
            auto* w = waiters->getAt(ctx, static_cast<int>(i));
            if (w && w != PROTO_NONE) rt.schedule(ctx, w);
        }
    }
}

// Read __state__ (0 pending / 1 resolved / 2 rejected). Takes `rt` so we can
// read the cached __state__ symbol off Bootstrap instead of paying a
// SymbolTable shard-lock + UTF-8 normalisation lookup per call.
long long readState(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* fut) {
    const proto::ProtoString* stateKey = rt.bootstrap().sym.state;
    auto* st = fut->getOwnAttributeDirect(ctx, stateKey);
    return (st && st != PROTO_NONE) ? st->asLong(ctx) : 0;
}

// Future>>wait
//
// Blocks until the future leaves the pending state, then returns __value__
// (resolved) or throws (rejected). Inside an actor handler it cooperatively
// yields (throws FutureYield) instead of blocking the worker thread.
const proto::ProtoObject* prim_Future_wait(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    const proto::ProtoString* valueKey =
        rt.bootstrap().sym.value;
    const proto::ProtoString* errorKey =
        rt.bootstrap().sym.error;

    SCHED_DIAG("prim_Future_wait ENTER future=" << r
               << " currentActor=" << rt.currentActor());

    // F6 v3 C: inside an actor handler, a pending future means cooperatively
    // yield — the engine snapshots the frame stack and returns the worker to
    // the ready queue. The state read is unsynchronised: if the future
    // settles between this read and the throw we yield needlessly and the
    // resume path immediately sees the settled state — a wasted round-trip,
    // never a correctness problem.
    //
    // If the future is ALREADY settled when an actor handler enters wait,
    // return the value synchronously — without touching the main-thread
    // wait state. `mainWaitingOn` is the main thread's per-future flag;
    // touching it from a worker thread (running an actor handler) would
    // clobber the main's outstanding wait and silently drop its wakeup.
    if (rt.currentActor() != nullptr) {
        long long s = readState(rt, ctx, r);
        if (s == 0) {
            throw FutureYield(r);
        }
        if (s == 1) {
            auto* v = r->getOwnAttributeDirect(ctx, valueKey);
            return v ? v : PROTO_NONE;
        }
        // s == 2: rejected — let the common path below throw.
        auto* e = r->getOwnAttributeDirect(ctx, errorKey);
        std::string msg = (e && e != PROTO_NONE)
            ? e->asString(ctx)->toStdString(ctx)
            : std::string("rejected");
        throw std::runtime_error("Future rejected: " + msg);
    }

    // Non-actor (main / foreground) path. Pure event-driven wait — no
    // sleep, no spin, no polling. The waiter announces itself via
    // `markMainWaitingOn(future)`, then blocks on `acquireMainWait()`;
    // the settler of THIS future calls `notifyMainWaiterIfFor(future)`
    // which releases the semaphore. The wait returns within one context
    // switch (~ 3 µs) of the settle.
    //
    // The seq_cst pair between `mainWaitingOn` (set by waiter, read by
    // settler) and the Future's `__state__` attribute (set by settler,
    // read by waiter) makes the loop race-free: either the waiter sees
    // the settled state before calling acquire (no syscall — break
    // immediately), or the settler sees `mainWaitingOn == future` and
    // issues a release. Spurious wakes from unrelated settles never
    // happen because the notify is conditional on pointer match.
    rt.markMainWaitingOn(r);
    while (readState(rt, ctx, r) == 0) {
        rt.acquireMainWait(ctx);
    }
    rt.markMainWaitingOn(nullptr);

    long long s = readState(rt, ctx, r);
    if (s == 1) {
        auto* v = r->getOwnAttributeDirect(ctx, valueKey);
        return v ? v : PROTO_NONE;
    }
    if (s == 2) {
        auto* e = r->getOwnAttributeDirect(ctx, errorKey);
        std::string msg = (e && e != PROTO_NONE)
            ? e->asString(ctx)->toStdString(ctx)
            : std::string("rejected");
        throw std::runtime_error("Future rejected: " + msg);
    }
    throw std::runtime_error("Future>>wait: unknown state");
}

// Future>>resolve: value — settle a pending future as resolved. Idempotent.
const proto::ProtoObject* prim_Future_resolve(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    if (argc != 1) throw std::runtime_error("Future>>resolve: expects 1 arg");
    settleFuture(rt, ctx, r, /*reject=*/false, a[0]);
    return r;
}

// Future>>rejectWith: error — settle a pending future as rejected. Idempotent.
const proto::ProtoObject* prim_Future_rejectWith(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 1) throw std::runtime_error("Future>>rejectWith: expects 1 arg");
    settleFuture(rt, ctx, r, /*reject=*/true, a[0]);
    return r;
}

// Future>>thenDo: aBlock — register a callback fired with the resolved value.
// Already resolved: fires now. Already rejected: no-op (catch: covers that).
const proto::ProtoObject* prim_Future_thenDo(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const* a,
                                              int argc) {
    if (argc != 1) throw std::runtime_error("Future>>thenDo: expects 1 arg");
    const proto::ProtoString* valueKey =
        rt.bootstrap().sym.value;
    const proto::ProtoString* thenCbsKey =
        rt.bootstrap().sym.thenCbs;
    auto* block = a[0];

    // Fast paths on an already-settled future (__state__ is published last by
    // settleFuture, so a non-zero read is authoritative).
    long long s = readState(rt, ctx, r);
    if (s == 2) return r;  // rejected — thenDo: never fires
    if (s == 1) {
        auto* v = r->getOwnAttributeDirect(ctx, valueKey);
        const proto::ProtoObject* args[] = { v ? v : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); } catch (...) {}
        return r;
    }
    // Pending (or mid-settle): CAS-append, or fire now if a resolve already
    // drained __then_cbs__. appendOrDrained == false only when a *resolve*
    // drained it (reject drains __catch_cbs__), so firing here is correct.
    if (!appendOrDrained(ctx, r, thenCbsKey, block)) {
        auto* v = r->getOwnAttributeDirect(ctx, valueKey);
        const proto::ProtoObject* args[] = { v ? v : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); } catch (...) {}
    }
    return r;
}

// Future>>catch: aBlock — register a callback fired with the rejection error.
// Already rejected: fires now. Already resolved: no-op.
const proto::ProtoObject* prim_Future_catch(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a,
                                             int argc) {
    if (argc != 1) throw std::runtime_error("Future>>catch: expects 1 arg");
    const proto::ProtoString* errorKey =
        rt.bootstrap().sym.error;
    const proto::ProtoString* catchCbsKey =
        rt.bootstrap().sym.catchCbs;
    auto* block = a[0];

    long long s = readState(rt, ctx, r);
    if (s == 1) return r;  // resolved — catch: never fires
    if (s == 2) {
        auto* e = r->getOwnAttributeDirect(ctx, errorKey);
        const proto::ProtoObject* args[] = { e ? e : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); } catch (...) {}
        return r;
    }
    if (!appendOrDrained(ctx, r, catchCbsKey, block)) {
        auto* e = r->getOwnAttributeDirect(ctx, errorKey);
        const proto::ProtoObject* args[] = { e ? e : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); } catch (...) {}
    }
    return r;
}

// Future>>new — a first-class, manually-resolvable pending future. Routes
// through STRuntime::newFuture so the result carries the full attribute
// layout (__state__ == 0, __value__/__error__ == nil) and behaves exactly
// like a future produced by an actor send.
const proto::ProtoObject* prim_Future_new(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject*,
                                           const proto::ProtoObject* const*, int) {
    return rt.newFuture(ctx);
}

} // anon

// Park `waiterActor` on `fut`'s __waiters__ list (called by the engine's
// FutureYield catch path, after it has snapshotted __suspended_frame__ on the
// actor). Returns true if the actor was parked — a settler will reschedule
// it; false if the future had already settled, in which case the caller must
// schedule the actor itself so the resume path consumes the settled value.
//
// Lock-free: a CAS-append into __waiters__. If the list has already been
// drained to PROTO_NONE the future is settled — return false. The exactly-once
// handoff is the same sentinel protocol used for callbacks.
//
// Declared `extern` at the ExecutionEngine catch site; the linker connects
// them.
bool appendFutureWaiter(STRuntime& rt,
                        proto::ProtoContext* ctx,
                        const proto::ProtoObject* fut,
                        const proto::ProtoObject* waiterActor) {
    if (!fut || !waiterActor) return false;
    const proto::ProtoString* waitersKey = rt.bootstrap().sym.waiters;
    // Fast path: an already-settled future is never parked on.
    if (readState(rt, ctx, fut) != 0) {
        SCHED_DIAG("appendFutureWaiter future=" << fut << " actor="
                   << waiterActor << " parked=0 (already settled)");
        return false;
    }
    bool parked = appendOrDrained(ctx, fut, waitersKey, waiterActor);
    SCHED_DIAG("appendFutureWaiter future=" << fut << " actor="
               << waiterActor << " parked=" << parked);
    return parked;
}

// drainOne settle entry points — identical lock-free settle as the user-facing
// Future>>resolve: / rejectWith: primitives.
void resolveFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                            const proto::ProtoObject* future,
                            const proto::ProtoObject* value) {
    settleFuture(rt, ctx, future, /*reject=*/false, value);
}

void rejectFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                           const proto::ProtoObject* future,
                           const proto::ProtoObject* error) {
    settleFuture(rt, ctx, future, /*reject=*/true, error);
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
    // `Future new` — a first-class, manually-resolvable promise. Overrides the
    // inherited Object>>new so the result carries the full future machinery.
    bindPrimitive(rt, b.futureProto, "new",
                  reg.registerPrim(prim_Future_new));
}

} // namespace protoST
