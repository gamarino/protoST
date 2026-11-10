// TimerService.cpp -- see TimerService.h for the design.
#include "runtime/TimerService.h"

#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace protoST {

namespace {

// The longest single wait of the timer thread. A deadline further away (up to
// TimePoint::max(), for a saturated `ms`) is reached by waiting again, so no
// conversion of a far deadline to the platform's wait can overflow.
constexpr auto kMaxWait = std::chrono::hours(1);

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

void TimerService::schedule(proto::ProtoContext* ctx, TimePoint deadline,
                            const proto::ProtoObject* future, bool reject) {
    if (!future || future == PROTO_NONE) return;
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        id = nextId_++;
    }
    // Rooted before the heap can name it, so the timer thread always finds
    // the Future of an entry it pops. The allocation runs without the mutex.
    rootEntry(ctx, id, future);

    bool dropped = false, earliest = false, start = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            dropped = true;
        } else {
            heap_.push(Entry{deadline, id, reject});
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
    if (!t) throw std::runtime_error("Future: the timer thread could not be started");
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

void TimerService::fire(proto::ProtoContext* ctx, const Entry& e) {
    const Bootstrap& b = rt_.bootstrap();
    // The Future is rooted by the sparse list until unrootEntry below; reading
    // the list and the Future's state allocates nothing.
    const proto::ProtoSparseList* list = sparseListOf(ctx, anchor_->getOwnAttributeDirect(ctx, key_));
    const proto::ProtoObject* future = list ? list->getAt(ctx, e.id) : nullptr;
    if (!future || future == PROTO_NONE) return;
    const proto::ProtoObject* state = future->getOwnAttributeDirect(ctx, b.sym.state);
    if (state && state != PROTO_NONE && state->asLong(ctx) != 0) {
        // Settled first (within: on a Future that answered in time): there is
        // nothing to do, and the entry stops rooting it now.
        unrootEntry(ctx, e.id);
        return;
    }
    // The task roots the Future from here on: pinned on this thread while it
    // is built, then in the runtime's task list once enqueued.
    const proto::ProtoObject* task =
        const_cast<proto::ProtoObject*>(b.objectProto)->newChild(ctx, /*isMutable=*/true);
    TransientPin pinTask(ctx, task);
    auto* t = const_cast<proto::ProtoObject*>(task);
    t->setAttribute(ctx, b.sym.future, future);
    t->setAttribute(ctx, b.sym.timeout, e.reject ? PROTO_TRUE : PROTO_FALSE);
    // Removed before the task is enqueued, so a waiter woken by the settle
    // never sees the entry still counted as pending.
    unrootEntry(ctx, e.id);
    rt_.enqueueTask(ctx, task);
}

void TimerService::run(proto::ProtoContext* ctx) {
    std::vector<Entry> due;
    for (;;) {
        due.clear();
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
                while (!heap_.empty() && heap_.top().deadline <= now) {
                    due.push_back(heap_.top());
                    heap_.pop();
                }
                if (!due.empty()) break;
                if (heap_.empty()) {
                    wake_.wait(lock);
                } else {
                    // A far deadline is never converted whole (kMaxWait).
                    const TimePoint top = heap_.top().deadline;
                    wake_.wait_until(lock, (top - now > kMaxWait) ? now + kMaxWait : top);
                }
            }
        }
        // Managed again, without the mutex: build and enqueue the tasks.
        for (const Entry& e : due) {
            try {
                fire(ctx, e);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "error: a timer could not be handed to the workers: %s\n",
                             ex.what());
            } catch (...) {
                std::fprintf(stderr, "error: a timer could not be handed to the workers\n");
            }
        }
        // Quiescent point: no pin is held, so this thread's young cells can
        // be submitted to the collector before it waits again.
        ctx->safepoint();
    }
}

} // namespace protoST
