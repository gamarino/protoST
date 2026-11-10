// TimerService.h -- deadlines for Futures on the monotonic clock.
//
// One per runtime, owned by STRuntime, its thread started on first use. It
// settles a Future at a deadline: resolved with nil (`Future after:`) or
// rejected with a TimeoutError (`Future >> within:`), through the same settle
// path every other resolver uses, so a Future settles once and the first
// completion wins.
//
// The thread never runs Smalltalk code
// ------------------------------------
// In protoST the thread that settles a Future runs its thenDo: / catch:
// callbacks. So when a deadline passes the timer thread only builds a small
// task object naming the Future and enqueues it on the worker pool
// (STRuntime::enqueueTask); a worker runs the settle and therefore the
// callbacks, with the normal GC safepoints. A slow or blocking callback holds
// a worker, never the timer thread, and cannot delay the next deadline.
//
// Threads, locks and the collector
// --------------------------------
// The thread is a protoCore ProtoThread (space.newThread), so it is a managed
// thread with a context of its own. The deadline heap is a plain C++ priority
// queue guarded by `mutex_`; the mutex guards the heap and the start / stop
// state only, and it is never held across a protoCore allocation (the task is
// built and the rooting structure updated outside it). The timer thread waits
// on `wake_` (a condition variable, timed on steady_clock) inside a protoCore
// unmanaged region, so it counts as parked for any stop-the-world phase; the
// region is opened before the mutex is taken and closed after it is released.
//
// Rooting (single-root pinning)
// -----------------------------
// The C++ heap holds only a deadline and an entry id. The Future itself hangs
// off one pinned structure: a ProtoSparseList (entry id -> Future) stored
// under one attribute of the runtime's live registry, updated by compare-and-
// swap. A Future therefore stays alive while its timer is pending, with no
// root handle per timer. When the timer fires, the task is built (and pinned
// on the timer thread's context) before the entry is removed, and enqueued
// after; the task list then roots the Future until a worker settles it.
//
// Cancellation. schedule() stamps the entry id on the Future (`__timer_id__`);
// whoever else settles the Future first calls cancel(), from settleFuture, and
// the Future stops being rooted by the service at once. The heap entry (an id
// and a deadline) is not searched for: it is skipped when it comes due and
// finds no rooted Future.
//
// A failed hand-over. If building or enqueueing the task fails (a protoCore
// allocation failure), the entry stays rooted -- it is unrooted only after
// the enqueue succeeded -- and is retried every kRetryDelay, with a
// rate-limited report on stderr: the Future settles as soon as memory allows,
// never on the timer thread (that would run its callbacks there), and never
// silently not at all. No exception leaves the timer thread.
//
// Shutdown
// --------
// stop() is called by ~STRuntime after the worker pool is joined. Timers still
// pending are dropped: their Futures never settle, and the timer thread waits
// for no deadline, so the service itself never keeps the process alive. A
// schedule() after stop() is dropped too. One caveat comes from the joins
// before it, not from the service: a worker blocked in a nested wait
// (waitHelping, e.g. waitWithin: inside a block of an actor method) is joined
// only once that wait ends, so ~STRuntime can be delayed up to that deadline.
//
// One service, and so one timer thread, per STRuntime. A process normally has
// one runtime; a guest runtime in a host's space (STRuntime(ProtoSpace&)) has
// its own service.
#ifndef PROTOST_TIMER_SERVICE_H
#define PROTOST_TIMER_SERVICE_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoObject;
class ProtoString;
class ProtoThread;
}

namespace protoST {

class STRuntime;

class TimerService {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // `anchor` is the runtime's pinned live registry and `key` the attribute
    // of it that holds the pending-entry ProtoSparseList (installed by the
    // runtime before any schedule).
    TimerService(STRuntime& rt, const proto::ProtoObject* anchor, const proto::ProtoString* key);
    ~TimerService();

    TimerService(const TimerService&)            = delete;
    TimerService& operator=(const TimerService&) = delete;

    // Settle `future` at `deadline`: resolve it with nil, or reject it with a
    // TimeoutError when `reject` is true; `ms` is the delay asked for, which
    // the TimeoutError's text states. `ctx` is the calling thread's own
    // context (the entry is rooted with an allocation on it). A deadline in
    // the past fires at once; a Future already settled is not scheduled.
    // Starts the timer thread on first use; throws std::runtime_error when
    // the thread cannot be created. After stop() it does nothing.
    void schedule(proto::ProtoContext* ctx, TimePoint deadline,
                  const proto::ProtoObject* future, bool reject, long long ms);

    // The Future of entry `id` settled by other means: stop rooting it.
    // Idempotent; a no-op for an entry that already fired. Called by
    // settleFuture with the id the Future carries (`__timer_id__`).
    void cancel(proto::ProtoContext* ctx, std::uint64_t id);

    // Ends the timer thread and joins it, dropping every pending timer.
    // Called once, by ~STRuntime, on the runtime's root context, after the
    // worker pool is joined. Idempotent.
    void stop(proto::ProtoContext* ctx);

    // Timers whose Future is still rooted by the service (scheduled, not yet
    // handed to the worker pool), read on `ctx`, the calling thread's own
    // context. For tests and diagnostics.
    unsigned long pendingCount(proto::ProtoContext* ctx) const;

    // Body of the timer thread. Public for the ProtoThread trampoline only.
    void run(proto::ProtoContext* ctx);

private:
    struct Entry {
        TimePoint     deadline;
        std::uint64_t id;       // key in the rooting sparse list; also FIFO order
        bool          reject;
        long long     ms;       // the delay asked for, for the TimeoutError text
    };
    // Earliest deadline on top; equal deadlines in scheduling order.
    struct Later {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.deadline != b.deadline) return a.deadline > b.deadline;
            return a.id > b.id;
        }
    };
    enum class State { Idle, Starting, Running, Stopped };

    // The rooting sparse list: add or remove one entry by compare-and-swap.
    void rootEntry(proto::ProtoContext* ctx, std::uint64_t id, const proto::ProtoObject* future);
    void unrootEntry(proto::ProtoContext* ctx, std::uint64_t id);
    // Hand one due entry to the worker pool (on the timer thread), or drop it
    // when its Future settled or was cancelled. Throws when an allocation
    // fails; the entry is then still rooted and run() retries it.
    void fire(proto::ProtoContext* ctx, const Entry& e);
    // Build the settle task for `future` (rooted by entry e) and enqueue it;
    // the entry is unrooted only after the enqueue succeeded.
    void handOver(proto::ProtoContext* ctx, const Entry& e, const proto::ProtoObject* future);
    // One stderr line for a failed hand-over: the first at once, then at most
    // one per second with the count of those suppressed. Timer thread only.
    void reportHandOverFailure(const char* detail) noexcept;
    void startThread(proto::ProtoContext* ctx);

    STRuntime&                 rt_;
    const proto::ProtoObject*  anchor_;
    const proto::ProtoString*  key_;

    mutable std::mutex         mutex_;      // guards heap_, state_, thread_
    std::condition_variable    wake_;       // new earliest deadline, or stop
    std::condition_variable    started_;    // Starting -> Running / Idle / Stopped
    std::priority_queue<Entry, std::vector<Entry>, Later> heap_;
    State                      state_   = State::Idle;
    bool                       stopping_ = false;
    const proto::ProtoThread*  thread_  = nullptr;
    std::uint64_t              nextId_  = 1;     // guarded by mutex_
    // reportHandOverFailure's rate limit (timer thread only).
    bool                       reportedOnce_      = false;
    TimePoint                  lastReport_{};
    std::uint64_t              suppressedReports_ = 0;
};

} // namespace protoST

#endif // PROTOST_TIMER_SERVICE_H
