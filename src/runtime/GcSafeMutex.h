// GcSafeMutex.h — F6 v3 E4: GC-safe std::mutex acquisition.
//
// Companion to GcSafeBlocking.h. E2 made condition-variable SLEEPS GC-safe;
// this header closes the SAME class of bug for blocking std::mutex
// ACQUISITION.
//
// The deadlock E2 left open
// ------------------------
// protoCore runs a stop-the-world (STW) tracing GC: a cycle proceeds only
// once every mutator thread has parked at a cooperative safepoint (inside
// allocCell / ProtoContext::safepoint), i.e. once
//
//     parkedThreads >= runningThreads
//
// A protoST worker thread blocked in std::mutex::lock() is in EXACTLY the
// same predicament as one asleep on a foreign condition variable: it is still
// counted in ProtoSpace::runningThreads, yet — stuck in the kernel futex of a
// plain std::mutex — it can never reach a safepoint to bump parkedThreads.
//
// The concrete cycle (confirmed via gdb on the 120-link cooperative chain):
//
//   1. Worker A acquires a protoST std::mutex (a future's cv mutex, a per-
//      actor lock, or schedMu) and, still holding it, allocates — newList /
//      appendLast / setAttribute → allocCell — which hits a GC safepoint and
//      PARKS worker A while it still owns the mutex.
//   2. Worker B tries to lock the SAME mutex. std::mutex::lock() is NOT a
//      protoCore safepoint, so B blocks in the kernel while still counted as
//      a running mutator.
//   3. The GC waits forever for B to park; B waits forever for A to release
//      the mutex; A waits forever for the GC cycle to finish. Total deadlock.
//
// The fix
// -------
// Acquire any such mutex GC-safely: try_lock first (the uncontended fast path
// needs no GC interaction at all); if that fails, enter the GC-blocking
// region (enterGcBlocking — leaves the running set so STW quorum is computed
// without this thread), do the blocking lock(), then exit the region
// (exitGcBlocking — rejoins the running set and parks at a safepoint if a STW
// began while we were blocked).
//
// CRITICAL USAGE RULES (inherited from GcSafeBlocking.h, both load-bearing):
//
//  1. enterGcBlocking..exitGcBlocking brackets a window in which the GC may
//     run a full STW cycle WITHOUT waiting for this thread. The ONLY thing
//     done inside that window here is the blocking std::mutex::lock() — a
//     pure futex wait, no protoCore heap access. Once exitGcBlocking returns
//     the thread is a running mutator again and may touch ProtoObjects.
//
//  2. exitGcBlocking calls safepoint(), which may park the thread. By the
//     time gcSafeLock returns, this thread OWNS `m`. That is fine and
//     intended: the whole point is that the holder may now park at a
//     safepoint while owning the lock — every OTHER contender reaches the
//     lock through gcSafeLock too, so a contender blocked on `m` has already
//     left the running set and does not stall GC quorum. The lock-ordering
//     rule of GcSafeBlocking.h rule 2 ("release every protoST lock before
//     exitGcBlocking") concerned a DIFFERENT lock held across a cv sleep; it
//     does not forbid exitGcBlocking from running as the final step of
//     acquiring the very lock being taken.
#ifndef PROTOST_GC_SAFE_MUTEX_H
#define PROTOST_GC_SAFE_MUTEX_H

#include "GcSafeBlocking.h"

#include <mutex>

namespace protoST {

// Acquire `m`, GC-safely, for the protoST thread whose protoCore context is
// `ctx`. Fast path: an uncontended try_lock — no GC interaction whatsoever.
// Slow path: leave the GC running set, block on lock(), rejoin + safepoint().
//
// `ctx` may be nullptr (e.g. a call site with no protoCore context); in that
// case enter/exitGcBlocking are no-ops and this degrades to a plain lock(),
// which is correct — a thread with no ProtoContext is not a GC mutator.
inline void gcSafeLock(proto::ProtoContext* ctx, std::mutex& m) {
    if (m.try_lock()) return;       // uncontended — no GC dance needed
    enterGcBlocking(ctx);           // leave the running set: GC quorum excludes us
    m.lock();                       // may block in the kernel arbitrarily long
    exitGcBlocking(ctx);            // rejoin the running set + park if a STW began
}

// RAII guard: gcSafeLock on construction, unlock on destruction. Drop-in
// replacement for std::lock_guard<std::mutex> at contended protoST sites.
//
// The two-argument constructor takes a `std::mutex&` and always locks. The
// pointer-overloaded constructor takes a `std::mutex*` and locks only when it
// is non-null — for the pre-T4 "future may have no cv" fallback sites that
// previously used a conditionally-engaged std::unique_lock.
//
// Non-copyable / non-movable, like std::lock_guard.
class GcSafeLockGuard {
    proto::ProtoContext* ctx_;
    std::mutex* m_;
public:
    GcSafeLockGuard(proto::ProtoContext* ctx, std::mutex& m)
        : ctx_(ctx), m_(&m) {
        gcSafeLock(ctx_, *m_);
    }
    // Conditional form: `m` may be nullptr, in which case no lock is taken
    // and the destructor is a no-op.
    GcSafeLockGuard(proto::ProtoContext* ctx, std::mutex* m)
        : ctx_(ctx), m_(m) {
        if (m_) gcSafeLock(ctx_, *m_);
    }
    ~GcSafeLockGuard() { if (m_) m_->unlock(); }

    GcSafeLockGuard(const GcSafeLockGuard&) = delete;
    GcSafeLockGuard& operator=(const GcSafeLockGuard&) = delete;
    GcSafeLockGuard(GcSafeLockGuard&&) = delete;
    GcSafeLockGuard& operator=(GcSafeLockGuard&&) = delete;
};

} // namespace protoST

#endif // PROTOST_GC_SAFE_MUTEX_H
