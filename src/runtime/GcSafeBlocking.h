// GcSafeBlocking.h — F6 v3 E2: GC-safe blocking region.
//
// protoCore runs a stop-the-world (STW) tracing GC. Its Phase 1
// (ProtoSpace.cpp gcThreadLoop) sets `stwFlag` and then blocks until
//
//     parkedThreads >= runningThreads
//
// i.e. every mutator thread must reach a cooperative safepoint (allocCell or
// ProtoContext::safepoint) so the collector can mark a quiescent heap.
//
// A protoST thread that goes to sleep on a *non-protoCore* condition
// variable — the per-future FutureCV in Future>>wait, or the scheduler
// `schedCv` an idle worker waits on — is invisible to that handshake: it is
// still counted in `runningThreads`, yet being parked on a foreign cv it can
// never call allocCell/safepoint to bump `parkedThreads`. If a deferred GC
// cycle begins while such a thread is asleep, the Phase-1 quorum can never
// be met, `stwFlag` is pinned `true` forever, and every other thread that
// subsequently allocates parks permanently in allocCell — total deadlock.
// (Empirically this surfaced on cooperative actor chains of depth >= ~110,
// the point at which cumulative allocation first crosses protoCore's GC
// trigger threshold while a thread is blocked.)
//
// enterGcBlocking / exitGcBlocking bracket such a sleep as a JVM-style
// "blocking safe region": between the two calls the thread is removed from
// `runningThreads`, so the STW quorum is computed only over threads that CAN
// actually park.
//
//   enterGcBlocking(ctx): decrements `runningThreads` and nudges `gcCV` (a GC
//     already waiting in Phase 1 must re-evaluate its quorum against the
//     lowered bound).
//   exitGcBlocking(ctx):  re-increments `runningThreads`, then calls
//     `safepoint()` so that, if a STW began while the thread was blocked, it
//     parks correctly BEFORE the caller touches any ProtoObject.
//
// CRITICAL USAGE RULES (both load-bearing — violating either reintroduces
// the deadlock or corrupts the heap):
//
//  1. NO protoCore heap access (no ProtoObject read/write, no allocation)
//     may happen between enterGcBlocking and exitGcBlocking. The GC can run a
//     full STW cycle in that window WITHOUT waiting for this thread; touching
//     an object then races the mark phase. Only foreign waits — a std::mutex
//     lock or a std::condition_variable sleep — are permitted inside.
//
//  2. exitGcBlocking calls `safepoint()`, which may park the thread. That
//     park must NOT happen while the caller holds any protoST std::mutex
//     (a future's cv mutex, the scheduler `schedMu`, an actor lock): a thread
//     blocked on a plain std::mutex is not itself at a safepoint, so a STW
//     park while holding such a lock would wedge every other contender and
//     the GC quorum could never be reached. Release every protoST lock
//     before calling exitGcBlocking.
//
// Safety: the thread's GC roots stay reachable for the whole region — its
// ProtoContext stays registered in `space->threads` (stack still scanned)
// and protoST keeps live actors/futures reachable from its single
// live-registry root pinned in the asyncRoots ProtoRootSet. Leaving the
// running set never risks reclamation; it only
// tells the collector "do not wait for me to reach a safepoint while I
// sleep."
#ifndef PROTOST_GC_SAFE_BLOCKING_H
#define PROTOST_GC_SAFE_BLOCKING_H

#include "protoCore.h"

#include <atomic>
#include <mutex>

namespace protoST {

// Leave the GC running set. Call immediately before a foreign-cv / mutex
// sleep. Must be paired with exactly one exitGcBlocking on every path.
inline void enterGcBlocking(proto::ProtoContext* ctx) {
    proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
    if (!space) return;
    space->runningThreads.fetch_sub(1, std::memory_order_acq_rel);
    // A GC may already be parked in Phase 1 waiting on the old (higher)
    // runningThreads value; nudge it to re-check now that this thread no
    // longer needs to reach a safepoint.
    std::lock_guard<std::recursive_mutex> gcl(proto::ProtoSpace::globalMutex);
    space->gcCV.notify_all();
}

// Re-join the GC running set and, if a stop-the-world is in progress, park at
// a safepoint before returning. The caller MUST hold no protoST std::mutex
// when calling this (see rule 2 above).
inline void exitGcBlocking(proto::ProtoContext* ctx) {
    proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
    if (!space) return;
    // Re-join the running set BEFORE the safepoint so the GC sees a
    // consistent count, then park if a STW is in progress.
    space->runningThreads.fetch_add(1, std::memory_order_acq_rel);
    ctx->safepoint();
}

} // namespace protoST

#endif // PROTOST_GC_SAFE_BLOCKING_H
