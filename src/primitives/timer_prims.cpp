// Timer primitives (protoST 0.9.0): the private Future selectors behind
// `Future after:` and `Future >> within:` (lib/kernel/future.st), and the
// worker-side half of a timer, runTimerTask.
//
//   aFuture __resolveNilAfter: ms     resolve aFuture with nil in ms milliseconds
//   aFuture __rejectTimeoutAfter: ms  in ms milliseconds, unless aFuture settled
//                                     first, send it __timedOutAfter: ms
//                                     (future.st), which rejects it with a
//                                     TimeoutError
//
// Both answer the receiver. A receiver that is already settled schedules
// nothing. `ms` is a non-negative SmallInteger, measured on the monotonic
// clock (std::chrono::steady_clock); a deadline beyond the clock's range is
// clamped to its end, which in practice never comes.
//
// The deadline is kept by the runtime's TimerService; when it passes, the
// timer thread hands a task to the worker pool, and a worker calls
// runTimerTask. The timer thread itself never runs Smalltalk code, so the
// TimeoutError is built here, on the worker, by the kernel's __timedOutAfter:
// (the class and the text are the kernel's, and C++ looks no global up by name).

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "primitives/ArgSupport.h"
#include "runtime/Bootstrap.h"
#include "runtime/TimerService.h"
#include "protoCore.h"

#include <chrono>
#include <stdexcept>
#include <string>

namespace protoST {

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

// Defined in future_prims.cpp: the lock-free settle (first completion wins).
void resolveFutureFromDrain(STRuntime& rt, proto::ProtoContext* ctx,
                            const proto::ProtoObject* future,
                            const proto::ProtoObject* value);

namespace {

using PO = proto::ProtoObject;

bool isSettled(STRuntime& rt, proto::ProtoContext* ctx, const PO* future) {
    const PO* s = future->getOwnAttributeDirect(ctx, rt.bootstrap().sym.state);
    return s && s != PROTO_NONE && s->asLong(ctx) != 0;
}

// The deadline `ms` milliseconds from now on the monotonic clock. `who` is the
// public selector, for the error text; `n` receives the milliseconds.
TimerService::TimePoint deadlineAfter(proto::ProtoContext* ctx, const PO* ms, const char* who,
                                      long long& n) {
    n = smallIntegerArg(ctx, ms, who, "the milliseconds");
    if (n < 0)
        throw std::runtime_error(errorPrefix(who) + "the milliseconds must not be negative, not "
                                 + std::to_string(n));
    const TimerService::TimePoint now = TimerService::Clock::now();
    const auto room = std::chrono::duration_cast<std::chrono::milliseconds>(
        TimerService::TimePoint::max() - now).count();
    if (n >= room) return TimerService::TimePoint::max();
    return now + std::chrono::milliseconds(n);
}

const PO* scheduleOn(STRuntime& rt, proto::ProtoContext* ctx, const PO* r,
                     const PO* const* a, int argc, bool reject, const char* who) {
    if (argc != 1) throw std::runtime_error(std::string(who) + " expects 1 argument");
    long long ms = 0;
    const TimerService::TimePoint deadline = deadlineAfter(ctx, a[0], who, ms);
    if (!isSettled(rt, ctx, r)) rt.timerService().schedule(ctx, deadline, r, reject, ms);
    return r;
}

// Future>>__resolveNilAfter: ms (Future class >> after:).
const PO* prim_Future_resolveNilAfter(STRuntime& rt, proto::ProtoContext* ctx, const PO* r,
                                      const PO* const* a, int argc) {
    return scheduleOn(rt, ctx, r, a, argc, /*reject=*/false, "Future class>>after:");
}

// Future>>__rejectTimeoutAfter: ms (Future >> within:).
const PO* prim_Future_rejectTimeoutAfter(STRuntime& rt, proto::ProtoContext* ctx, const PO* r,
                                         const PO* const* a, int argc) {
    return scheduleOn(rt, ctx, r, a, argc, /*reject=*/true, "Future>>within:");
}

} // namespace

// A timer task, on a pool worker (STRuntime::runOneTask). The task is pinned
// by the caller and roots its Future.
void runTimerTask(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* task) {
    const Bootstrap& b = rt.bootstrap();
    const PO* future = task->getOwnAttributeDirect(ctx, b.sym.future);
    if (!future || future == PROTO_NONE) return;
    if (task->getOwnAttributeDirect(ctx, b.sym.timeout) != PROTO_TRUE) {
        resolveFutureFromDrain(rt, ctx, future, PROTO_NONE);
        return;
    }
    // A Future that settled meanwhile keeps its result: nothing to build.
    if (isSettled(rt, ctx, future)) return;
    const PO* ms = task->getOwnAttributeDirect(ctx, b.sym.ms);
    const PO* args[] = { (ms && ms != PROTO_NONE) ? ms : ctx->fromLong(0) };
    bool understood = false;
    sendDynamic(rt, ctx, future, b.sym.timedOutAfter, args, 1, &understood);
    if (!understood)
        throw std::runtime_error("Future: a deadline passed, but Future does not understand "
                                 "__timedOutAfter: (lib/kernel/future.st is not loaded)");
}

void installTimerPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.futureProto, "__resolveNilAfter:",
                  reg.registerPrim(prim_Future_resolveNilAfter));
    bindPrimitive(rt, b.futureProto, "__rejectTimeoutAfter:",
                  reg.registerPrim(prim_Future_rejectTimeoutAfter));
}

} // namespace protoST
