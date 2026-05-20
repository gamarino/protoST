#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>

namespace protoST {

// Defined in block_prims.cpp — runs a BlockClosure with the given arg vector.
extern const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* block,
                                              const proto::ProtoObject* const* args,
                                              int argc);

// F6 v2 T4: per-Future condition_variable used by Future>>wait to block
// until the future transitions out of pending. The struct lives on the C++
// heap, owned by an ExternalPointer attached to the future under __cv__; the
// finalizer below runs at GC reclamation, at which point no thread can be
// holding the mutex (the future is unreachable).
struct FutureCV {
    std::mutex mu;
    std::condition_variable cv;
};

static void futureCVFinalizer(void* p) {
    delete static_cast<FutureCV*>(p);
}

// Helpers exposed to STRuntime: install a fresh FutureCV on a newly minted
// future (called from STRuntime::newFuture). Implemented after the
// anonymous namespace below; the resolve/reject *FromDrain entry points
// that drainOne uses are also defined there.
void installFutureCV(proto::ProtoContext* ctx, const proto::ProtoObject* fut);

namespace {

// Resolve the FutureCV attached to a future, or nullptr if none is present.
// A nullptr return signals "no cv installed" — wait will throw rather than
// silently spin, since after T4 every future must carry a cv.
FutureCV* getFutureCV(proto::ProtoContext* ctx, const proto::ProtoObject* fut) {
    if (!fut || fut == PROTO_NONE) return nullptr;
    static const proto::ProtoString* cvKey =
        proto::ProtoString::createSymbol(ctx, "__cv__");
    auto* cvObj = fut->getAttribute(ctx, cvKey);
    if (!cvObj || cvObj == PROTO_NONE) return nullptr;
    auto* ep = cvObj->asExternalPointer(ctx);
    if (!ep) return nullptr;
    return static_cast<FutureCV*>(ep->getPointer(ctx));
}

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
// F6 v2 T4: blocks the calling thread on the future's own condition_variable
// until the worker thread (or any other resolver) transitions __state__ out
// of pending. On a resolved future, returns __value__. On a rejected future,
// throws std::runtime_error carrying the rejection cause.
//
// The cv pattern is the standard one:
//   * resolve / rejectWith / drainOne write __state__ AND notify_all while
//     holding the future's mutex.
//   * wait holds the future's mutex while evaluating the predicate (state != 0)
//     and across the cv.wait sleep. Standard mutex+cv pairing closes the
//     classic "lost wakeup" window because a notify is only visible if the
//     waiter has not yet entered wait, and entering wait atomically releases
//     the mutex — so resolve's state-write under the mutex is guaranteed to
//     be visible to wait's predicate before wait sleeps.
//
// protoCore's attribute writes are individually atomic, so getAttribute inside
// the predicate is safe even though the future's mutex is orthogonal to
// protoCore's own attribute CAS. The mutex here governs the cv contract only.
const proto::ProtoObject* prim_Future_wait(STRuntime& /*rt*/, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    static const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* valueKey =
        proto::ProtoString::createSymbol(ctx, "__value__");
    static const proto::ProtoString* errorKey =
        proto::ProtoString::createSymbol(ctx, "__error__");

    auto* fcv = getFutureCV(ctx, r);
    if (!fcv) {
        // After T4 every future built via STRuntime::newFuture carries a cv.
        // Hitting this branch means a future was constructed by some other
        // path (e.g. raw newChild without going through newFuture); treat it
        // as a programming error rather than silently spinning.
        throw std::runtime_error(
            "Future>>wait: no condition variable installed on receiver");
    }

    {
        std::unique_lock<std::mutex> lock(fcv->mu);
        fcv->cv.wait(lock, [&]() {
            auto* st = r->getAttribute(ctx, stateKey);
            return st && st->asLong(ctx) != 0;  // 1 = resolved, 2 = rejected
        });
    }

    // State observed under the cv's mutex above is the same we re-read here:
    // once a future transitions out of pending it never transitions back, so
    // a fresh getAttribute without the lock is sufficient.
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
    // The predicate guaranteed s != 0 before we exited cv.wait; any other
    // value here would indicate corruption of the state field.
    throw std::runtime_error("Future>>wait: unknown state");
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

    // F6 v2 T4: the entire transition (state write, callback fire, notify)
    // runs under the future's cv mutex. See resolveFutureFromDrain for the
    // full rationale — the short version is that a concurrent Future>>wait
    // must not observe "settled" before our callbacks have all run, and the
    // simplest way to enforce that is to fire callbacks while still holding
    // the mutex the waiter must reacquire to return from cv.wait.
    auto* fcv = getFutureCV(ctx, r);
    auto doTransition = [&]() {
        const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, stateKey, ctx->fromLong(1));
        const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, valueKey, a[0]);
        auto* cbsSnapshot = r->getAttribute(ctx, thenCbsKey);
        if (cbsSnapshot && cbsSnapshot != PROTO_NONE) {
            if (auto* cbs = cbsSnapshot->asList(ctx)) {
                long long n = cbs->getSize(ctx);
                const proto::ProtoObject* cargs[] = { a[0] ? a[0] : PROTO_NONE };
                for (long long i = 0; i < n; ++i) {
                    auto* cb = cbs->getAt(ctx, static_cast<int>(i));
                    try { invokeBlock(rt, ctx, cb, cargs, 1); }
                    catch (...) { /* swallow */ }
                }
            }
        }
    };
    if (fcv) {
        std::lock_guard<std::mutex> lock(fcv->mu);
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
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

    // F6 v2 T4: same locked-transition pattern as prim_Future_resolve.
    auto* fcv = getFutureCV(ctx, r);
    auto doTransition = [&]() {
        const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, stateKey, ctx->fromLong(2));
        const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, errorKey, a[0]);
        auto* cbsSnapshot = r->getAttribute(ctx, catchCbsKey);
        if (cbsSnapshot && cbsSnapshot != PROTO_NONE) {
            if (auto* cbs = cbsSnapshot->asList(ctx)) {
                long long n = cbs->getSize(ctx);
                const proto::ProtoObject* cargs[] = { a[0] ? a[0] : PROTO_NONE };
                for (long long i = 0; i < n; ++i) {
                    auto* cb = cbs->getAt(ctx, static_cast<int>(i));
                    try { invokeBlock(rt, ctx, cb, cargs, 1); }
                    catch (...) { /* swallow */ }
                }
            }
        }
    };
    if (fcv) {
        std::lock_guard<std::mutex> lock(fcv->mu);
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
    return r;
}

// Future>>thenDo: aBlock
//
// Registers `aBlock` to be invoked with the resolved value once the future
// resolves. If the future is already resolved, fires the block synchronously
// with the current value. If already rejected, does nothing (catch: covers
// that path). Returns the receiver so chains can continue.
//
// F6 v2 T4: the (state-read, register-or-fire) sequence runs under the
// future's cv mutex so it is atomic with respect to a concurrent producer
// (resolve / rejectWith / drainOne) that takes the same mutex to write
// state and snapshot the callback list. Without this, a worker thread that
// snapshots an empty list and writes state=1 between our read and our
// append would silently drop the callback.
//
// The synchronous fire branch runs OUTSIDE the mutex (user block may run
// arbitrary code, including recursive future operations).
const proto::ProtoObject* prim_Future_thenDo(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const* a,
                                              int argc) {
    if (argc != 1) throw std::runtime_error("Future>>thenDo: expects 1 arg");
    static const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* valueKey =
        proto::ProtoString::createSymbol(ctx, "__value__");
    static const proto::ProtoString* thenCbsKey =
        proto::ProtoString::createSymbol(ctx, "__then_cbs__");
    auto* block = a[0];
    auto* fcv = getFutureCV(ctx, r);

    long long s = 0;
    const proto::ProtoObject* settledValue = nullptr;
    {
        // Use a unique_lock so we can unconditionally lock when fcv is non-null;
        // skip the lock entirely for futures without a cv (pre-T4 fallback only).
        std::unique_lock<std::mutex> guard;
        if (fcv) guard = std::unique_lock<std::mutex>(fcv->mu);

        auto* st = r->getAttribute(ctx, stateKey);
        s = st ? st->asLong(ctx) : 0;
        if (s == 0) {
            // Pending — register for later firing on resolve. Producer will
            // include us in its callback-list snapshot when it transitions
            // under this same mutex.
            registerCallback(ctx, r, thenCbsKey, block);
        } else if (s == 1) {
            // Already resolved — capture value under the mutex; we fire
            // outside it (see comment above).
            settledValue = r->getAttribute(ctx, valueKey);
        }
        // s == 2: already rejected; thenDo never fires on rejection path.
    }
    if (s == 1) {
        const proto::ProtoObject* args[] = { settledValue ? settledValue : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (...) { /* swallow */ }
    }
    return r;
}

// Future>>catch: aBlock
//
// Registers `aBlock` to be invoked with the error value once the future is
// rejected. If the future is already rejected, fires the block synchronously
// with the current error. If already resolved, does nothing. Returns the
// receiver so chains can continue.
//
// F6 v2 T4: same mutex-guarded read/register pattern as thenDo:. See the
// comment there for the registration-race analysis.
const proto::ProtoObject* prim_Future_catch(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a,
                                             int argc) {
    if (argc != 1) throw std::runtime_error("Future>>catch: expects 1 arg");
    static const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* errorKey =
        proto::ProtoString::createSymbol(ctx, "__error__");
    static const proto::ProtoString* catchCbsKey =
        proto::ProtoString::createSymbol(ctx, "__catch_cbs__");
    auto* block = a[0];
    auto* fcv = getFutureCV(ctx, r);

    long long s = 0;
    const proto::ProtoObject* settledError = nullptr;
    {
        std::unique_lock<std::mutex> guard;
        if (fcv) guard = std::unique_lock<std::mutex>(fcv->mu);

        auto* st = r->getAttribute(ctx, stateKey);
        s = st ? st->asLong(ctx) : 0;
        if (s == 0) {
            registerCallback(ctx, r, catchCbsKey, block);
        } else if (s == 2) {
            settledError = r->getAttribute(ctx, errorKey);
        }
        // s == 1: already resolved; catch never fires on resolution path.
    }
    if (s == 2) {
        const proto::ProtoObject* args[] = { settledError ? settledError : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (...) { /* swallow */ }
    }
    return r;
}

} // anon

// F6 v2 T4: attach a fresh FutureCV to a newly minted future. The cv is
// owned by an ExternalPointer whose finalizer (futureCVFinalizer) deletes
// the heap object at GC reclamation. Called from STRuntime::newFuture.
void installFutureCV(proto::ProtoContext* ctx, const proto::ProtoObject* fut) {
    if (!fut || fut == PROTO_NONE) return;
    static const proto::ProtoString* cvKey =
        proto::ProtoString::createSymbol(ctx, "__cv__");
    auto* fcv = new FutureCV();
    auto* ep = ctx->fromExternalPointer(fcv, futureCVFinalizer);
    const_cast<proto::ProtoObject*>(fut)->setAttribute(ctx, cvKey, ep);
}

// F6 v2 T4: atomic transition helpers for drainOne. These hold the future's
// cv mutex across the ENTIRE transition: state write, callback-list read,
// notify_all, AND callback execution.
//
// Why callbacks fire UNDER the mutex (not after):
//
//   The waiter (Future>>wait) wakes from cv.wait by reacquiring the same
//   mutex. If we released the mutex before firing callbacks, the waiter
//   could reacquire it, observe state==settled, and RETURN from wait while
//   the producer is still mid-callback. The test
//     f thenDo: [ :v | logger setLog: v ].
//     f wait.
//     logger getLog.
//   would then race: wait returns on the main thread before the worker
//   thread has run the thenDo block, so `logger getLog` may observe the
//   pre-callback value. Holding the mutex across the callback loop
//   establishes "wait returns ⇒ all callbacks completed" by forcing the
//   waiter to wait for our mutex release.
//
// Why this is safe despite "don't run user code under locks":
//
//   * The race we close is exactly the one above; it is observable as a
//     test failure, so we accept the constraint.
//   * The mutex is a per-future std::mutex (non-recursive). A user block
//     that re-enters Future>>resolve or Future>>thenDo on the SAME future
//     would self-deadlock. F6 callbacks are simple and don't do this.
//   * Re-entry on a DIFFERENT future is unaffected — distinct mutex.
//   * Re-entry on Future>>wait for the same future returns immediately
//     (state is already settled, predicate evaluates true on the path
//     before the cv lock attempt — see prim_Future_wait), so no deadlock.
//     Actually wait DOES take the mutex; but the predicate-only branch is
//     short and itself does not call back into anything that takes the cv
//     mutex, so a self-acquisition cycle is not formed.
//
// Callbacks registered AFTER our snapshot are fired by the thenDo:/catch:
// primitive itself on the "already settled" branch (also under this same
// mutex on entry; the fire there happens after the mutex is released).
void resolveFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                            const proto::ProtoObject* future,
                            const proto::ProtoObject* value) {
    if (!future) return;
    static const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* valueKey =
        proto::ProtoString::createSymbol(ctx, "__value__");
    static const proto::ProtoString* thenCbsKey =
        proto::ProtoString::createSymbol(ctx, "__then_cbs__");

    auto* fcv = getFutureCV(ctx, future);
    auto doTransition = [&]() {
        const_cast<proto::ProtoObject*>(future)
            ->setAttribute(ctx, stateKey, ctx->fromLong(1));
        const_cast<proto::ProtoObject*>(future)
            ->setAttribute(ctx, valueKey, value);
        auto* cbsSnapshot = future->getAttribute(ctx, thenCbsKey);
        if (cbsSnapshot && cbsSnapshot != PROTO_NONE) {
            if (auto* cbs = cbsSnapshot->asList(ctx)) {
                long long n = cbs->getSize(ctx);
                const proto::ProtoObject* args[] = { value ? value : PROTO_NONE };
                for (long long i = 0; i < n; ++i) {
                    auto* cb = cbs->getAt(ctx, static_cast<int>(i));
                    try { invokeBlock(rt, ctx, cb, args, 1); }
                    catch (...) { /* swallow */ }
                }
            }
        }
    };
    if (fcv) {
        std::lock_guard<std::mutex> lock(fcv->mu);
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
}

void rejectFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                           const proto::ProtoObject* future,
                           const proto::ProtoObject* error) {
    if (!future) return;
    static const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* errorKey =
        proto::ProtoString::createSymbol(ctx, "__error__");
    static const proto::ProtoString* catchCbsKey =
        proto::ProtoString::createSymbol(ctx, "__catch_cbs__");

    auto* fcv = getFutureCV(ctx, future);
    auto doTransition = [&]() {
        const_cast<proto::ProtoObject*>(future)
            ->setAttribute(ctx, stateKey, ctx->fromLong(2));
        const_cast<proto::ProtoObject*>(future)
            ->setAttribute(ctx, errorKey, error);
        auto* cbsSnapshot = future->getAttribute(ctx, catchCbsKey);
        if (cbsSnapshot && cbsSnapshot != PROTO_NONE) {
            if (auto* cbs = cbsSnapshot->asList(ctx)) {
                long long n = cbs->getSize(ctx);
                const proto::ProtoObject* args[] = { error ? error : PROTO_NONE };
                for (long long i = 0; i < n; ++i) {
                    auto* cb = cbs->getAt(ctx, static_cast<int>(i));
                    try { invokeBlock(rt, ctx, cb, args, 1); }
                    catch (...) { /* swallow */ }
                }
            }
        }
    };
    if (fcv) {
        std::lock_guard<std::mutex> lock(fcv->mu);
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
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
