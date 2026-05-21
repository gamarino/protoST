#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/FutureYield.h"
#include "runtime/SchedDiag.h"
#include "runtime/GcSafeBlocking.h"
#include "runtime/GcSafeMutex.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <chrono>
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

// F6 v3 C+D: snapshot the __waiters__ list (callers always run this with
// the future's cv mutex held, so the snapshot is atomic with respect to
// other transitions) and clear the slot. The returned list is the set of
// actors to schedule once the mutex is released.
//
// Symbol resolved fresh from the live ctx, not a function-local static:
// protoCore interns symbols per-ProtoSpace, so a static would dangle for
// every STRuntime after the first. The engine's FutureYield catch and
// drainOne's resume path resolve the identical name; consistency within a
// runtime is what makes the waiter handoff correct.
const proto::ProtoList*
takeAndClearWaitersLocked(proto::ProtoContext* ctx,
                          const proto::ProtoObject* fut) {
    const proto::ProtoString* waitersKey =
        proto::ProtoString::createSymbol(ctx, "__waiters__");
    // F6 v3 E5: `waitersKey` is freshly interned and held across the
    // setAttribute below (which allocates a sparse-list node on the mutable
    // future, plus a fresh empty list). Pin it.
    TransientPin pinWaitersKey(
        ctx, reinterpret_cast<const proto::ProtoObject*>(waitersKey));
    auto* w = fut->getAttribute(ctx, waitersKey);
    if (!w || w == PROTO_NONE) return nullptr;
    auto* list = w->asList(ctx);
    if (!list || list->getSize(ctx) == 0) return nullptr;
    const_cast<proto::ProtoObject*>(fut)->setAttribute(
        ctx, waitersKey, ctx->newList()->asObject(ctx));
    // NOTE: the returned `list` is the OLD waiters list — once the slot above
    // is overwritten it is no longer reachable via `fut`. Every caller pins
    // it immediately on return (it is iterated across rt.schedule, which
    // allocates). See the TransientPin at each call site.
    return list;
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
    // F6 v3 E5: `list` may be a fresh ctx->newList() transient held across
    // appendLast; `newList` is held across setAttribute on the mutable
    // future. Pin both.
    TransientPin pinList(
        ctx, reinterpret_cast<const proto::ProtoObject*>(list));
    auto* newList = list->appendLast(ctx, block);
    TransientPin pinNewList(
        ctx, reinterpret_cast<const proto::ProtoObject*>(newList));
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
const proto::ProtoObject* prim_Future_wait(STRuntime& rt, proto::ProtoContext* ctx,
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

    // F6 v3 C: if we are currently executing an actor message handler
    // (STRuntime::currentActor() is non-null on this thread) AND the
    // future has not yet settled, COOPERATIVELY YIELD by throwing
    // FutureYield. The engine catches this at the dispatch-loop
    // boundary, snapshots its frame stack onto the actor, and rethrows
    // to drainOne — which returns the worker thread to the ready queue
    // without blocking on this future.
    //
    // We probe state under the cv mutex so the resolve/reject path
    // (which holds the same mutex while writing state) cannot land
    // between our state read and the throw. If we observed pending
    // outside the mutex and the future settled before our throw, we
    // would yield even though the value was already available — a
    // performance bug but not a correctness one (the future's
    // resolveFutureFromDrain will schedule us via __waiters__ and the
    // resume path will see state == settled). Doing it under the mutex
    // simply avoids that wasted round-trip.
    SCHED_DIAG("prim_Future_wait ENTER future=" << r
               << " currentActor=" << rt.currentActor());
    if (rt.currentActor() != nullptr) {
        // F6 v3 E4: acquire the future cv mutex GC-safely. A producer
        // (resolveFutureFromDrain / Future>>resolve:) holds this same mutex
        // across invokeBlock — i.e. across protoCore allocation — and may
        // park at a GC safepoint while holding it. A plain std::mutex::lock()
        // here would block this thread in the kernel while it is still
        // counted as a running mutator, stalling the STW quorum forever.
        // See GcSafeMutex.h.
        bool yield = false;
        {
            GcSafeLockGuard lock(ctx, fcv->mu);
            auto* st = r->getAttribute(ctx, stateKey);
            long long s = st ? st->asLong(ctx) : 0;
            SCHED_DIAG("prim_Future_wait future=" << r << " state=" << s
                       << " (actor path)");
            // s == 0: still pending — cooperatively yield. We must DROP the
            // lock before throwing: the engine's catch site appends us to
            // the future's __waiters__ list, which re-acquires this same
            // mutex via appendFutureWaiterLocked. Holding it across the
            // throw would self-deadlock. The GcSafeLockGuard releases on
            // scope exit below, before the throw.
            yield = (s == 0);
            // Already settled — fall through to the synchronous read below.
        }
        if (yield) throw FutureYield(r);
    }

    // F6 v3 E2: GC-safe blocking wait. Sleeping on the future's own
    // std::condition_variable while still counted in ProtoSpace::runningThreads
    // would deadlock a concurrent stop-the-world GC (it waits for every
    // running thread to reach a safepoint, which a thread asleep on a foreign
    // cv never can). The GcBlockingRegion enter/exit calls below remove this
    // thread from the running set for the duration of each genuine sleep.
    // See GcSafeBlocking.h for the full rationale.
    //
    // Two invariants this loop must preserve, both learned the hard way:
    //
    //  (1) NO protoCore heap access while outside the running set. Between
    //      `enterGcBlocking` and `exitGcBlocking` the GC may run a full
    //      stop-the-world cycle WITHOUT waiting for this thread (that is the
    //      whole point — the thread is discounted from the quorum). Touching
    //      a ProtoObject in that window races the collector's mark phase and
    //      corrupts live state. The state probe (`getAttribute`) is therefore
    //      done strictly while the thread is still a running mutator; only
    //      the pure `cv.wait_for` sleep (a futex wait — no heap) runs inside
    //      the region.
    //
    //  (2) The exit's `safepoint()` must NOT run while `fcv->mu` is held. A
    //      producer (resolveFutureFromDrain / Future>>resolve:) blocked on
    //      `fcv->mu` is not itself at a safepoint, so a STW park holding
    //      `fcv->mu` would wedge the GC quorum. We therefore unlock `fcv->mu`
    //      before exitGcBlocking.
    //
    // The sleep is bounded (wait_for 50 ms) so a notify delivered in the
    // unlock / region-teardown window is never lost — the next loop turn
    // re-probes the state.
    for (;;) {
        bool settled = false;
        {
            // F6 v3 E4: acquire `fcv->mu` GC-safely. The plain
            // `std::unique_lock` constructor would block this thread in the
            // kernel — not at a protoCore safepoint — while a producer that
            // holds `fcv->mu` across an allocation is parked, stalling the
            // STW quorum. gcSafeLock leaves the running set for the blocking
            // acquire; we then adopt the already-owned mutex into a
            // unique_lock so `cv.wait_for` can release/re-acquire it.
            gcSafeLock(ctx, fcv->mu);
            std::unique_lock<std::mutex> lock(fcv->mu, std::adopt_lock);
            // Running-set member here: heap access is safe.
            auto* st = r->getAttribute(ctx, stateKey);
            if (st && st->asLong(ctx) != 0) {
                settled = true;  // 1 = resolved, 2 = rejected
            } else {
                // Still pending. Leave the running set, then sleep on the
                // future cv with NO heap access until woken. `lock` is held
                // across enterGcBlocking (only globalMutex is taken there,
                // never another future mutex — no lock cycle) and across the
                // bounded sleep; it is released before exitGcBlocking so the
                // safepoint there runs with no future lock held.
                enterGcBlocking(ctx);
                fcv->cv.wait_for(lock, std::chrono::milliseconds(50));
            }
        }
        if (settled) break;
        // `fcv->mu` released above; rejoin the running set + safepoint here.
        exitGcBlocking(ctx);
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
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* valueKey =
        proto::ProtoString::createSymbol(ctx, "__value__");
    static const proto::ProtoString* thenCbsKey =
        proto::ProtoString::createSymbol(ctx, "__then_cbs__");
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
    const proto::ProtoList* waitersSnapshot = nullptr;
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
        // F6 v3 D: parity with resolveFutureFromDrain — user-facing
        // Future>>resolve: must also wake yielded waiters.
        waitersSnapshot = takeAndClearWaitersLocked(ctx, r);
    };
    if (fcv) {
        GcSafeLockGuard lock(ctx, fcv->mu);  // F6 v3 E4: GC-safe acquire
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
    if (waitersSnapshot) {
        // F6 v3 E5: `waitersSnapshot` is the detached old __waiters__ list —
        // no longer reachable via the future after takeAndClearWaitersLocked
        // overwrote the slot. It is iterated here across rt.schedule, which
        // allocates inside registryAdd. Pin it for the iteration.
        TransientPin pinWaiters(
            ctx, reinterpret_cast<const proto::ProtoObject*>(waitersSnapshot));
        long long n = waitersSnapshot->getSize(ctx);
        for (long long i = 0; i < n; ++i) {
            auto* w = waitersSnapshot->getAt(ctx, static_cast<int>(i));
            if (w && w != PROTO_NONE) rt.schedule(ctx, w);
        }
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
        proto::ProtoString::createSymbol(ctx, "__state__");
    static const proto::ProtoString* errorKey =
        proto::ProtoString::createSymbol(ctx, "__error__");
    static const proto::ProtoString* catchCbsKey =
        proto::ProtoString::createSymbol(ctx, "__catch_cbs__");
    auto* st = r->getAttribute(ctx, stateKey);
    long long s = st ? st->asLong(ctx) : 0;
    if (s != 0) return r;  // already settled — no-op

    // F6 v2 T4: same locked-transition pattern as prim_Future_resolve.
    auto* fcv = getFutureCV(ctx, r);
    const proto::ProtoList* waitersSnapshot = nullptr;
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
        waitersSnapshot = takeAndClearWaitersLocked(ctx, r);
    };
    if (fcv) {
        GcSafeLockGuard lock(ctx, fcv->mu);  // F6 v3 E4: GC-safe acquire
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
    if (waitersSnapshot) {
        // F6 v3 E5: pin the detached waiters list across rt.schedule.
        TransientPin pinWaiters(
            ctx, reinterpret_cast<const proto::ProtoObject*>(waitersSnapshot));
        long long n = waitersSnapshot->getSize(ctx);
        for (long long i = 0; i < n; ++i) {
            auto* w = waitersSnapshot->getAt(ctx, static_cast<int>(i));
            if (w && w != PROTO_NONE) rt.schedule(ctx, w);
        }
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
        // F6 v3 E4: GC-safe acquire. fcv may be null (pre-T4 fallback) — the
        // pointer-form GcSafeLockGuard locks only when it is non-null.
        GcSafeLockGuard guard(ctx, fcv ? &fcv->mu : nullptr);

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
        // F6 v3 E4: GC-safe acquire (see prim_Future_thenDo).
        GcSafeLockGuard guard(ctx, fcv ? &fcv->mu : nullptr);

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

// Future>>new
//
// `Future new` must yield a *usable* pending future — a first-class promise —
// not a bare newChild of futureProto. A bare child lacks the `__state__`,
// `__value__`, `__error__` and `__cv__` attributes that resolve:/rejectWith:/
// wait depend on; sending `resolve:` to it fails with "Object is not an
// integer type" because the state probe reads a nil (PROTO_NONE) slot and
// calls asLong on it. Routing through STRuntime::newFuture installs the full
// future machinery, so a manually-constructed Future behaves identically to
// one produced by an actor send: it starts pending, settles via resolve: /
// rejectWith:, and wait blocks until then.
const proto::ProtoObject* prim_Future_new(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject*,
                                           const proto::ProtoObject* const*, int) {
    return rt.newFuture(ctx);
}

} // anon

// F6 v3 C: append an actor to the future's __waiters__ list while
// holding the future's cv mutex. The engine's FutureYield catch path
// calls this after recording __suspended_frame__ on the actor, so by the
// time we return the actor is fully parked and any future state
// transition will see it in __waiters__. Without the mutex a producer
// thread could (a) snapshot an old __waiters__, (b) finish its
// transition, (c) we land our actor into __waiters__ — and the actor
// would never wake.
//
// Returns true if the actor was successfully parked on __waiters__;
// false if the future had already settled between the FutureYield throw
// and our acquisition of the mutex (in which case the caller must
// schedule the actor itself to consume the resolved/rejected value).
// This atomic state-check + append closes the race where:
//   * Future>>wait reads state==0, drops the mutex, throws FutureYield;
//   * a producer on another thread takes the mutex, transitions to
//     settled, snapshots+clears __waiters__ (empty), notifies cv;
//   * we re-acquire the mutex and would append a waiter to a SETTLED
//     future whose transition path has already passed — parking the
//     actor forever.
// By re-reading state under the mutex BEFORE appending and signalling
// the caller via the return value, we collapse that window.
//
// Declared `extern` inside ExecutionEngine.cpp at the catch site; the
// linker connects them.
bool appendFutureWaiterLocked(proto::ProtoContext* ctx,
                              const proto::ProtoObject* fut,
                              const proto::ProtoObject* waiterActor) {
    if (!fut || !waiterActor) return false;
    // Symbols resolved fresh from the live ctx (per-ProtoSpace interning;
    // see takeAndClearWaitersLocked).
    const proto::ProtoString* waitersKey =
        proto::ProtoString::createSymbol(ctx, "__waiters__");
    const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    // F6 v3 E5: both keys are freshly interned and held across the
    // getAttribute/appendLast/setAttribute sequence inside `doAppend`. Pin
    // them for the function's whole scope (covers the lambda invocation).
    TransientPin pinWaitersKey(
        ctx, reinterpret_cast<const proto::ProtoObject*>(waitersKey));
    TransientPin pinStateKey(
        ctx, reinterpret_cast<const proto::ProtoObject*>(stateKey));
    auto* fcv = getFutureCV(ctx, fut);
    bool parked = false;
    auto doAppend = [&]() {
        auto* st = fut->getAttribute(ctx, stateKey);
        long long s = st ? st->asLong(ctx) : 0;
        if (s != 0) {
            // Future already settled. Don't append — caller schedules
            // the actor manually so the resume path sees the settled
            // state.
            parked = false;
            return;
        }
        auto* existing = fut->getAttribute(ctx, waitersKey);
        const proto::ProtoList* list = nullptr;
        if (existing && existing != PROTO_NONE) list = existing->asList(ctx);
        if (!list) list = ctx->newList();
        // F6 v3 E5: `list` may be a fresh ctx->newList() transient held
        // across appendLast; `newList` is then held across setAttribute.
        TransientPin pinList(
            ctx, reinterpret_cast<const proto::ProtoObject*>(list));
        auto* newList = list->appendLast(ctx, waiterActor);
        TransientPin pinNewList(
            ctx, reinterpret_cast<const proto::ProtoObject*>(newList));
        const_cast<proto::ProtoObject*>(fut)
            ->setAttribute(ctx, waitersKey, newList->asObject(ctx));
        parked = true;
    };
    if (fcv) {
        GcSafeLockGuard lock(ctx, fcv->mu);  // F6 v3 E4: GC-safe acquire
        doAppend();
    } else {
        doAppend();
    }
    SCHED_DIAG("appendFutureWaiterLocked future=" << fut
               << " actor=" << waiterActor
               << " parked=" << parked);
    return parked;
}

// F6 v2 T4: attach a fresh FutureCV to a newly minted future. The cv is
// owned by an ExternalPointer whose finalizer (futureCVFinalizer) deletes
// the heap object at GC reclamation. Called from STRuntime::newFuture.
void installFutureCV(proto::ProtoContext* ctx, const proto::ProtoObject* fut) {
    if (!fut || fut == PROTO_NONE) return;
    static const proto::ProtoString* cvKey =
        proto::ProtoString::createSymbol(ctx, "__cv__");
    auto* fcv = new FutureCV();
    auto* ep = ctx->fromExternalPointer(fcv, futureCVFinalizer);
    // F6 v3 E5: `ep` is held across setAttribute on the mutable future. Pin it.
    TransientPin pinEp(ctx, ep);
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
    const proto::ProtoList* waitersSnapshot = nullptr;
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
        // F6 v3 D: snapshot + clear __waiters__ before releasing the
        // mutex; scheduling happens outside the mutex below.
        waitersSnapshot = takeAndClearWaitersLocked(ctx, future);
    };
    if (fcv) {
        GcSafeLockGuard lock(ctx, fcv->mu);  // F6 v3 E4: GC-safe acquire
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
    // F6 v3 D: reschedule every yielded actor that was waiting on this
    // future. schedule() takes schedMu; doing so outside fcv->mu keeps
    // the established lock order (schedMu disjoint from per-future
    // mutexes) consistent with drainOne and the SEND fast-path.
    long long nWaiters = waitersSnapshot ? waitersSnapshot->getSize(ctx) : 0;
    SCHED_DIAG("resolveFutureFromDrain future=" << future
               << " value=" << value << " waiters=" << nWaiters);
    if (waitersSnapshot) {
        // F6 v3 E5: pin the detached waiters list across rt.schedule.
        TransientPin pinWaiters(
            ctx, reinterpret_cast<const proto::ProtoObject*>(waitersSnapshot));
        long long n = waitersSnapshot->getSize(ctx);
        for (long long i = 0; i < n; ++i) {
            auto* a = waitersSnapshot->getAt(ctx, static_cast<int>(i));
            if (a && a != PROTO_NONE) rt.schedule(ctx, a);
        }
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
    const proto::ProtoList* waitersSnapshot = nullptr;
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
        // F6 v3 D: same waiter-handoff as resolveFutureFromDrain — a
        // yielded actor that was waiting on a future that ends up
        // rejected still needs to be resumed (the resume path will
        // detect state==2 and rethrow inside the resumed frame).
        waitersSnapshot = takeAndClearWaitersLocked(ctx, future);
    };
    if (fcv) {
        GcSafeLockGuard lock(ctx, fcv->mu);  // F6 v3 E4: GC-safe acquire
        doTransition();
        fcv->cv.notify_all();
    } else {
        doTransition();
    }
    if (waitersSnapshot) {
        // F6 v3 E5: pin the detached waiters list across rt.schedule.
        TransientPin pinWaiters(
            ctx, reinterpret_cast<const proto::ProtoObject*>(waitersSnapshot));
        long long n = waitersSnapshot->getSize(ctx);
        for (long long i = 0; i < n; ++i) {
            auto* a = waitersSnapshot->getAt(ctx, static_cast<int>(i));
            if (a && a != PROTO_NONE) rt.schedule(ctx, a);
        }
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
    // `Future new` — a first-class, manually-resolvable promise. Overrides the
    // inherited Object>>new so the result carries the full future machinery.
    bindPrimitive(rt, b.futureProto, "new",
                  reg.registerPrim(prim_Future_new));
}

} // namespace protoST
