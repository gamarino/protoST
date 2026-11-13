// TimerService.cpp -- see TimerService.h for the design.
#include "runtime/TimerService.h"
#include "runtime/NativeError.h"

#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace protoST {

namespace {

// The longest single wait of the timer thread. A deadline further away (up to
// TimePoint::max(), for a saturated `ms`) is reached by waiting again, so no
// conversion of a far deadline to the platform's wait can overflow.
constexpr auto kMaxWait = std::chrono::hours(1);

// How long a timer whose hand-over to the workers failed waits before the
// next attempt.
constexpr auto kRetryDelay = std::chrono::milliseconds(10);

// Entry of the timer ProtoThread: args[0] is an ExternalPointer to the
// service.
const proto::ProtoObject* timerThreadMain(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* /*self*/,
                                          const proto::ParentLink* /*parentLink*/,
                                          const proto::ProtoArguments* args,
                                          const proto::ProtoKeywordArguments* /*kwargs*/) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* first = args->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ep = first ? first->asExternalPointer(ctx) : nullptr;
    if (!ep) return PROTO_NONE;
    auto* service = static_cast<TimerService*>(ep->getPointer(ctx));
    if (service) service->run(ctx);
    return PROTO_NONE;
}

const proto::ProtoSparseList* sparseListOf(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    return (o && o != PROTO_NONE) ? o->asSparseList(ctx) : nullptr;
}

} // namespace

TimerService::TimerService(STRuntime& rt, const proto::ProtoObject* anchor,
                           const proto::ProtoString* key)
    : rt_(rt), anchor_(anchor), key_(key) {}

TimerService::~TimerService() {
    // ~STRuntime stops the service (and joins its thread) before the runtime's
    // members go away.
    assert(thread_ == nullptr && "TimerService destroyed with its thread running");
}

void TimerService::rootEntry(proto::ProtoContext* ctx, std::uint64_t id,
                             const proto::ProtoObject* future) {
    TransientPin pinFuture(ctx, future);
    auto* anchor = const_cast<proto::ProtoObject*>(anchor_);
    for (;;) {
        const proto::ProtoObject* cur = anchor->getOwnAttributeDirect(ctx, key_);
        TransientPin pinCur(ctx, cur);
        const proto::ProtoSparseList* list = sparseListOf(ctx, cur);
        if (!list) list = ctx->newSparseList();
        TransientPin pinList(ctx, reinterpret_cast<const proto::ProtoObject*>(list));
        const proto::ProtoSparseList* next = list->setAt(ctx, id, future);
        TransientPin pinNext(ctx, reinterpret_cast<const proto::ProtoObject*>(next));
        const proto::ProtoObject* nextObj = next->asObject(ctx);
        TransientPin pinNextObj(ctx, nextObj);
        if (anchor->setAttributeIfEqual(ctx, key_, cur, nextObj)) return;
    }
}

void TimerService::unrootEntry(proto::ProtoContext* ctx, std::uint64_t id) {
    auto* anchor = const_cast<proto::ProtoObject*>(anchor_);
    for (;;) {
        const proto::ProtoObject* cur = anchor->getOwnAttributeDirect(ctx, key_);
        const proto::ProtoSparseList* list = sparseListOf(ctx, cur);
        if (!list || !list->has(ctx, id)) return;
        TransientPin pinCur(ctx, cur);
        const proto::ProtoSparseList* next = list->removeAt(ctx, id);
        TransientPin pinNext(ctx, reinterpret_cast<const proto::ProtoObject*>(next));
        const proto::ProtoObject* nextObj = next->asObject(ctx);
        TransientPin pinNextObj(ctx, nextObj);
        if (anchor->setAttributeIfEqual(ctx, key_, cur, nextObj)) return;
    }
}

unsigned long TimerService::pendingCount(proto::ProtoContext* ctx) const {
    const proto::ProtoSparseList* list = sparseListOf(ctx, anchor_->getOwnAttributeDirect(ctx, key_));
    return list ? static_cast<unsigned long>(list->getSize(ctx)) : 0;
}

void TimerService::cancel(proto::ProtoContext* ctx, std::uint64_t id) {
    unrootEntry(ctx, id);
}

void TimerService::schedule(proto::ProtoContext* ctx, TimePoint deadline,
                            const proto::ProtoObject* future, bool reject, long long ms) {
    if (!future || future == PROTO_NONE) return;
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        id = nextId_++;
    }
    const Bootstrap& b = rt_.bootstrap();
    TransientPin pinFuture(ctx, future);
    // S40: a pending timer can release a capped wait; ends the quiescence
    // epoch before it is rooted (STRuntime::confirmStuck).
    rt_.noteProgress();
    // The id first, so a settler that wins the claim from here on finds it
    // and cancels; then the root, so the timer thread always finds the
    // Future of an entry it pops; then a look at the claim: a settle that
    // came before the root (whose cancel found nothing to remove) is caught
    // here. Every step runs without the mutex.
    const_cast<proto::ProtoObject*>(future)->setAttribute(
        ctx, b.sym.timerId, ctx->fromLong(static_cast<long long>(id)));
    rootEntry(ctx, id, future);
    if (future->getOwnAttributeDirect(ctx, b.sym.settling) == PROTO_TRUE) {
        unrootEntry(ctx, id);
        return;
    }

    bool dropped = false, earliest = false, start = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            dropped = true;
        } else {
            heap_.push(Entry{deadline, id, reject, ms});
            earliest = heap_.top().id == id;
            if (state_ == State::Idle) { state_ = State::Starting; start = true; }
        }
    }
    if (dropped) { unrootEntry(ctx, id); return; }
    // A new earliest deadline shortens the thread's current wait.
    if (earliest) wake_.notify_one();
    if (start) startThread(ctx);
}

void TimerService::startThread(proto::ProtoContext* ctx) {
    // newThread allocates (it can park for a collection), so it runs without
    // the mutex; `Starting` keeps other schedulers from starting a second
    // thread, and stop() waits for it to end.
    const proto::ProtoThread* t = nullptr;
    std::exception_ptr failed;
    try {
        const proto::ProtoList* args = ctx->newList();
        TransientPin pinArgs(ctx, reinterpret_cast<const proto::ProtoObject*>(args));
        args = args->appendLast(ctx, ctx->fromExternalPointer(this, nullptr));
        pinArgs.reset(reinterpret_cast<const proto::ProtoObject*>(args));
        const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "protoST-timer");
        t = rt_.space()->newThread(ctx, name, timerThreadMain, args, nullptr);
    } catch (...) {
        failed = std::current_exception();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_ = t;
        state_  = t ? State::Running : State::Idle;
    }
    started_.notify_all();
    if (failed) std::rethrow_exception(failed);
    // The entry just scheduled stays queued: the next schedule() tries to
    // start the thread again, and stop() drops it at exit.
    if (!t) throw NativeError("Future: the timer thread could not be started");
}

void TimerService::stop(proto::ProtoContext* ctx) {
    const proto::ProtoThread* t = nullptr;
    {
        // A scheduler may be inside newThread, parked for a collection; wait
        // for it unmanaged so that collection is not held up by this thread.
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_ == State::Stopped) return;
        stopping_ = true;
        wake_.notify_all();
        started_.wait(lock, [this] { return state_ != State::Starting; });
        t       = thread_;
        thread_ = nullptr;
        state_  = State::Stopped;
        // Pending timers are dropped; their entries stay in the rooting list
        // until the runtime's live registry goes away with the runtime.
        heap_ = decltype(heap_)();
    }
    if (t) {
        // The timer thread may be handing a task to the pool and request a
        // collection meanwhile; the join therefore runs unmanaged, as the
        // worker joins in ~STRuntime do.
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        const_cast<proto::ProtoThread*>(t)->join(ctx);
    }
}

void TimerService::reportHandOverFailure(const char* detail) noexcept {
    // First failure at once; then at most one line per second, saying how
    // many were folded into it. No allocation: this runs when memory is short.
    const TimePoint now = Clock::now();
    if (reportedOnce_ && now - lastReport_ < std::chrono::seconds(1)) {
        ++suppressedReports_;
        return;
    }
    if (suppressedReports_ > 0)
        std::fprintf(stderr, "error: a timer could not be handed to the workers (%s); retrying "
                             "(%llu similar messages suppressed)\n",
                     detail, static_cast<unsigned long long>(suppressedReports_));
    else
        std::fprintf(stderr, "error: a timer could not be handed to the workers (%s); retrying\n",
                     detail);
    reportedOnce_      = true;
    lastReport_        = now;
    suppressedReports_ = 0;
}

void TimerService::fire(proto::ProtoContext* ctx, const Entry& e) {
    const Bootstrap& b = rt_.bootstrap();
    // The Future is rooted by the sparse list until the entry is unrooted;
    // reading the list and the Future's state allocates nothing.
    const proto::ProtoSparseList* list = sparseListOf(ctx, anchor_->getOwnAttributeDirect(ctx, key_));
    if (!list || !list->has(ctx, e.id)) return;   // cancelled: skipped
    const proto::ProtoObject* future = list->getAt(ctx, e.id);
    if (!future || future == PROTO_NONE) return;
    if (future->getOwnAttributeDirect(ctx, b.sym.settling) == PROTO_TRUE) {
        // Settled (or being settled) first; its cancel may still be on its
        // way. Nothing to hand over; the entry stops rooting it now (if this
        // throws, run() retries and the entry is looked at again).
        unrootEntry(ctx, e.id);
        return;
    }
    handOver(ctx, e, future);
}

void TimerService::handOver(proto::ProtoContext* ctx, const Entry& e,
                            const proto::ProtoObject* future) {
    const Bootstrap& b = rt_.bootstrap();
    // The entry keeps rooting the Future until the enqueue has succeeded; the
    // task is pinned on this thread while it is built. Any failure up to the
    // enqueue leaves the entry exactly as it was, for the retry.
    const proto::ProtoObject* task =
        const_cast<proto::ProtoObject*>(b.objectProto)->newChild(ctx, /*isMutable=*/true);
    TransientPin pinTask(ctx, task);
    auto* t = const_cast<proto::ProtoObject*>(task);
    t->setAttribute(ctx, b.sym.future, future);
    t->setAttribute(ctx, b.sym.timeout, e.reject ? PROTO_TRUE : PROTO_FALSE);
    t->setAttribute(ctx, b.sym.ms, ctx->fromLong(e.ms));
    // enqueueTask allocates only before its compare-and-swap publishes the
    // task; once it returns, the task is queued and roots the Future.
    rt_.enqueueTask(ctx, task);
    // Queued. The settle unroots the entry as well (settleFuture cancels the
    // Future's __timer_id__, before it publishes the state, so no waiter
    // woken by it sees the entry pending); this unroot only makes it prompt.
    // It must not fail the hand-over: a retry would enqueue a second task.
    try {
        unrootEntry(ctx, e.id);
    } catch (...) {
        // Left to the settle's cancel.
    }
}

void TimerService::run(proto::ProtoContext* ctx) {
    // `due` holds the entries of the current round. An entry whose hand-over
    // failed stays in it (compacted in place: shrinking allocates nothing)
    // and is retried at `retryAt`; it is never put back on the heap, whose
    // push could itself fail.
    std::vector<Entry> due;
    TimePoint retryAt = TimePoint::max();
    for (;;) {
        {
            // Waiting is unmanaged: this thread counts as parked for any
            // stop-the-world phase. The region is opened before the mutex is
            // taken, so the mutex is released before the region ends (and
            // before any park on leaving it).
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;) {
                if (stopping_) return;
                const TimePoint now = Clock::now();
                bool collected = false;
                bool collectFailed = false;
                try {
                    // Copied before it is popped: if the copy cannot grow
                    // `due`, the entry stays on the heap.
                    while (!heap_.empty() && heap_.top().deadline <= now) {
                        due.push_back(heap_.top());
                        heap_.pop();
                        collected = true;
                    }
                } catch (...) {
                    // Out of memory for `due`: handle what was collected.
                    collectFailed = true;
                }
                if (collected || (!due.empty() && retryAt <= now)) break;
                TimePoint until = retryAt;
                if (collectFailed) {
                    // The heap's top is already due but could not be taken:
                    // waiting for its deadline would return at once and spin.
                    // Report (rate-limited, allocation-free) and try again
                    // after kRetryDelay.
                    reportHandOverFailure("no memory to take a due timer");
                    if (now + kRetryDelay < until) until = now + kRetryDelay;
                } else if (!heap_.empty() && heap_.top().deadline < until) {
                    until = heap_.top().deadline;
                }
                if (until == TimePoint::max()) {
                    wake_.wait(lock);
                } else {
                    // A far deadline is never converted whole (kMaxWait).
                    wake_.wait_until(lock, (until - now > kMaxWait) ? now + kMaxWait : until);
                }
            }
        }
        // Managed again, without the mutex: build and enqueue the tasks.
        // Nothing escapes this thread: a failed entry is kept for the retry.
        std::size_t kept = 0;
        for (std::size_t i = 0; i < due.size(); ++i) {
            bool done = false;
            try {
                fire(ctx, due[i]);
                done = true;
            } catch (const std::exception& ex) {
                // what(), not nativeErrorText: this path runs when memory is
                // short and must not copy the text. A hand-over failure is
                // the runtime's own (an allocation), never user text.
                reportHandOverFailure(ex.what());
            } catch (...) {
                reportHandOverFailure("an allocation failed");
            }
            if (!done) due[kept++] = due[i];
        }
        due.resize(kept);
        retryAt = kept ? Clock::now() + kRetryDelay : TimePoint::max();
        // Quiescent point: no pin is held, so this thread's young cells can
        // be submitted to the collector before it waits again.
        ctx->safepoint();
    }
}

} // namespace protoST
