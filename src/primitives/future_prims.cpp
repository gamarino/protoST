#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/FutureYield.h"
#include "runtime/SchedDiag.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/TransientPin.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/UnhandledSTException.h"
#include <cstdio>
#include "runtime/HandlerStack.h"
#include "protoCore.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace protoST {

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

// Defined below with the unobserved-rejection bookkeeping.
void markFutureObserved(proto::ProtoContext* ctx, const proto::ProtoObject* future);


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

// Raise a rejected Future's error in the waiter (defined below).
const proto::ProtoObject* raiseRejection(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* error);

namespace {

// A thenDo: / catch: callback runs when a future settles, where nobody can
// catch its error: report it on stderr (with the trace of an unhandled
// protoST error) instead of dropping it, and carry on with the next callback.
void reportCallbackError(const char* what, const std::exception* e) {
    std::string text = e ? describeUncaught(*e) : std::string("error: ") + what;
    std::fprintf(stderr, "error in a Future callback: %s\n",
                 text.rfind("error: ", 0) == 0 ? text.c_str() + 7 : text.c_str());
}

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
        catch (const std::exception& e) { reportCallbackError(e.what(), &e); }
        catch (...) { reportCallbackError("non-local exit from a callback", nullptr); }
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

// An actor is about to park on `awaited`. If the actor that will settle it is
// itself parked on a pending future whose settling actor is parked on ... the
// current actor, nobody can ever proceed: an actor that waits handles no other
// message (D37). Signal an Error in the current actor instead of hanging. Only
// pending futures are followed, so an actor that is about to resume never
// counts.
//
// Two actors can reach their waits at the same moment. Each first records what
// it awaits and only then walks the chain (with a full fence in between), so
// at least one of them sees the other's record and reports the cycle.
// Actors whose method is buried on this thread's stack under a message this
// thread is running for another actor while it waits (waitHelping). A buried
// actor cannot continue until everything above it returns.
thread_local std::vector<const proto::ProtoObject*> t_buriedActors;

bool isBuriedHere(const proto::ProtoObject* actor) {
    for (const auto* a : t_buriedActors) if (a == actor) return true;
    return false;
}

void detectWaitCycle(STRuntime& rt, proto::ProtoContext* ctx,
                     const proto::ProtoObject* self, const proto::ProtoObject* awaited) {
    const auto& sym = rt.bootstrap().sym;
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, sym.waitingOn, awaited);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const proto::ProtoObject* fut = awaited;
    for (int step = 0; step < 256 && fut && fut != PROTO_NONE; ++step) {
        if (readState(rt, ctx, fut) != 0) return;            // settled: no cycle
        const proto::ProtoObject* target = fut->getOwnAttributeDirect(ctx, sym.targetActor);
        if (!target || target == PROTO_NONE) return;          // not an actor message
        if (target == self || isBuriedHere(target)) {
            const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, sym.waitingOn, PROTO_NONE);
            throw std::runtime_error(
                "deadlock: this actor waits for a reply that can only come from an actor "
                "waiting on it (an actor that waits handles no other message; chain with "
                "thenDo: instead of waiting)");
        }
        fut = target->getAttribute(ctx, sym.waitingOn);       // what that actor awaits
    }
}

// Wait for `awaited` on an actor's worker without suspending the actor: while
// the future is pending, run other ready actors' messages on this thread
// (helping), and back off briefly when there are none. The current actor stays
// marked as running, so none of its own messages run meanwhile (one message
// at a time), and its __waiting_on__ record keeps cycles detectable.
const proto::ProtoObject* waitHelping(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* awaited) {
    const proto::ProtoObject* self = rt.currentActor();
    TransientPin pinAwaited(ctx, awaited);
    // This actor stays buried under whatever it helps; nested helping is
    // capped so a chain of waiting actors cannot exhaust the engine stack.
    constexpr std::size_t kMaxHelpingDepth = 16;
    struct Bury {
        const proto::ProtoObject* actor;
        explicit Bury(const proto::ProtoObject* a) : actor(a) { t_buriedActors.push_back(a); }
        ~Bury() { t_buriedActors.pop_back(); }
    } bury(self);
    struct ClearWaiting {
        STRuntime& rt; proto::ProtoContext* ctx; const proto::ProtoObject* actor;
        ~ClearWaiting() {
            const_cast<proto::ProtoObject*>(actor)->setAttribute(ctx, rt.bootstrap().sym.waitingOn, PROTO_NONE);
        }
    } clearWaiting{rt, ctx, self};
    int idle = 0;
    while (readState(rt, ctx, awaited) == 0) {
        if (t_buriedActors.size() > kMaxHelpingDepth) {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }
        bool helped = false;
        // The helped message belongs to another actor: this actor's exception
        // handlers, still on this thread's handler stack, must not catch its
        // errors.
        const std::vector<unsigned long> hidden = handlerStackDisableAll();
        try {
            helped = rt.drainOne(ctx);
        } catch (...) {
            handlerStackRestore(hidden);
            rt.setCurrentActor(self);
            throw;
        }
        handlerStackRestore(hidden);
        rt.setCurrentActor(self);
        if (helped) { idle = 0; continue; }
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        std::this_thread::sleep_for(std::chrono::microseconds(idle < 10 ? 20 : 500));
        ++idle;
    }
    const proto::ProtoString* valueKey = rt.bootstrap().sym.value;
    const proto::ProtoString* errorKey = rt.bootstrap().sym.error;
    if (readState(rt, ctx, awaited) == 1) {
        auto* v = awaited->getOwnAttributeDirect(ctx, valueKey);
        return v ? v : PROTO_NONE;
    }
    return raiseRejection(rt, ctx, awaited->getOwnAttributeDirect(ctx, errorKey));
}

// Future>>wait
//
// Blocks until the future leaves the pending state, then returns __value__
// (resolved) or throws (rejected). Inside an actor handler it cooperatively
// yields (throws FutureYield) instead of blocking the worker thread.
const proto::ProtoObject* prim_Future_wait(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    markFutureObserved(ctx, r);
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
            detectWaitCycle(rt, ctx, rt.currentActor(), r);
            // Suspending the actor snapshots the engine frames, but not the
            // state of a primitive between them (the position of a do:, the
            // C++ frames of ensure: or on:do:), so a wait inside a block that
            // a primitive evaluates must not suspend. It blocks this worker
            // instead, running other actors' messages meanwhile.
            if (ExecutionEngine::liveEnginesOnThisThread() > 1)
                return waitHelping(rt, ctx, r);
            throw FutureYield(r);
        }
        if (s == 1) {
            auto* v = r->getOwnAttributeDirect(ctx, valueKey);
            return v ? v : PROTO_NONE;
        }
        // s == 2: rejected.
        return raiseRejection(rt, ctx, r->getOwnAttributeDirect(ctx, errorKey));
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

    // Young-generation submission for the waiting (non-actor) thread. Its
    // context lives as long as the thread, so its young cells become
    // collectable only when submitted, and safepoint() submits them.
    // Submission is safe only where every live cell is reachable from a real
    // root. With exactly one engine on this thread, the C++ frames below this
    // primitive are the engine's send handler (receiver and arguments stay in
    // frame slots) and the top-level entry (its captured dictionary is
    // pinned). A wait nested under a primitive (`do:`, `collect:`, an import)
    // runs in a second engine, below C++ code that may hold cells in locals —
    // an unpinned collection iterator, for example — so it does not submit.
    if (ExecutionEngine::liveEnginesOnThisThread() == 1) {
        ctx->safepoint();
    }

    long long s = readState(rt, ctx, r);
    if (s == 1) {
        auto* v = r->getOwnAttributeDirect(ctx, valueKey);
        return v ? v : PROTO_NONE;
    }
    if (s == 2) return raiseRejection(rt, ctx, r->getOwnAttributeDirect(ctx, errorKey));
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
    markFutureObserved(ctx, r);
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
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (const std::exception& e) { reportCallbackError(e.what(), &e); }
        catch (...) { reportCallbackError("non-local exit from a callback", nullptr); }
        return r;
    }
    // Pending (or mid-settle): CAS-append, or fire now if a resolve already
    // drained __then_cbs__. appendOrDrained == false only when a *resolve*
    // drained it (reject drains __catch_cbs__), so firing here is correct.
    if (!appendOrDrained(ctx, r, thenCbsKey, block)) {
        auto* v = r->getOwnAttributeDirect(ctx, valueKey);
        const proto::ProtoObject* args[] = { v ? v : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (const std::exception& e) { reportCallbackError(e.what(), &e); }
        catch (...) { reportCallbackError("non-local exit from a callback", nullptr); }
    }
    return r;
}

// Future>>catch: aBlock — register a callback fired with the rejection error.
// Already rejected: fires now. Already resolved: no-op.
const proto::ProtoObject* prim_Future_catch(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a,
                                             int argc) {
    markFutureObserved(ctx, r);
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
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (const std::exception& e) { reportCallbackError(e.what(), &e); }
        catch (...) { reportCallbackError("non-local exit from a callback", nullptr); }
        return r;
    }
    if (!appendOrDrained(ctx, r, catchCbsKey, block)) {
        auto* e = r->getOwnAttributeDirect(ctx, errorKey);
        const proto::ProtoObject* args[] = { e ? e : PROTO_NONE };
        try { invokeBlock(rt, ctx, block, args, 1); }
        catch (const std::exception& e) { reportCallbackError(e.what(), &e); }
        catch (...) { reportCallbackError("non-local exit from a callback", nullptr); }
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

// Unobserved actor errors. A send to an actor answers a Future; when the
// actor's method raises, the drain rejects it. If no one ever waits on that
// Future or registers thenDo:/catch:, the error would vanish, so a rejection
// from the drain is recorded -- as text, keyed by the Future's identity (a
// mutable object keeps its address), never as an object pointer, which the GC
// would not trace -- and reported when the runtime ends. Observing a Future
// stamps it (a GC-managed attribute) and discards its record.
namespace {
std::mutex g_rejectionsMu;
std::unordered_map<std::uintptr_t, std::string> g_unobservedRejections;

// Symbols are interned per ProtoSpace: resolved per call, never cached.
const proto::ProtoString* observedKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__observed__");
}

std::string describeRejection(proto::ProtoContext* ctx, const proto::ProtoObject* error) {
    if (!error || error == PROTO_NONE) return "nil";
    if (const proto::ProtoString* str = error->asString(ctx)) return str->toStdString(ctx);
    const proto::ProtoString* msgKey = proto::ProtoString::createSymbol(ctx, "__message_text__");
    const proto::ProtoObject* m = error->getAttribute(ctx, msgKey);
    if (m && m != PROTO_NONE)
        if (const proto::ProtoString* ms = m->asString(ctx)) return ms->toStdString(ctx);
    return "an error without messageText";
}
} // namespace

// A rejection that is a protoST exception (an actor method's unhandled error)
// is re-signalled here, so the waiter's handlers see its class and
// messageText; any other rejection value raises an Error carrying its text.
const proto::ProtoObject* raiseRejection(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* error) {
    if (isExceptionInstance(rt, ctx, error)) {
        // Every waiter signals its own copy: signal stamps handler state on
        // the instance, and several threads may wait on the same Future.
        bool understood = false;
        const proto::ProtoObject* copy = sendDynamic(
            rt, ctx, error, proto::ProtoString::createSymbol(ctx, "shallowCopy"),
            nullptr, 0, &understood);
        return resignalException(rt, ctx, (understood && copy) ? copy : error);
    }
    throw std::runtime_error("Future rejected: " + describeRejection(ctx, error));
}

void markFutureObserved(proto::ProtoContext* ctx, const proto::ProtoObject* future) {
    if (!future || future == PROTO_NONE) return;
    future->setAttribute(ctx, observedKey(ctx), PROTO_TRUE);
    std::lock_guard<std::mutex> lock(g_rejectionsMu);
    g_unobservedRejections.erase(reinterpret_cast<std::uintptr_t>(future));
}

void reportUnobservedActorErrors() {
    std::lock_guard<std::mutex> lock(g_rejectionsMu);
    for (const auto& entry : g_unobservedRejections)
        std::fprintf(stderr, "warning: unhandled error in actor: %s\n", entry.second.c_str());
    g_unobservedRejections.clear();
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
    if (future && future != PROTO_NONE
        && future->getAttribute(ctx, observedKey(ctx)) != PROTO_TRUE) {
        std::lock_guard<std::mutex> lock(g_rejectionsMu);
        g_unobservedRejections[reinterpret_cast<std::uintptr_t>(future)] =
            describeRejection(ctx, error);
    }
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
