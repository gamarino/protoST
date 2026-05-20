#pragma once

// F6 v3 C: FutureYield — cooperative-yield exception thrown by Future>>wait
// when the receiver is still pending AND the calling thread is currently
// executing an actor message handler.
//
// Control-flow contract:
//
//   1. Future>>wait checks STRuntime::currentActor(). When non-null AND the
//      future's __state__ is 0 (pending), it throws FutureYield(receiver)
//      instead of blocking on the per-future condition variable.
//
//   2. ExecutionEngine::runLoop catches FutureYield at the dispatch loop
//      boundary. On catch it:
//        * takes a snapshot of frames_ via snapshotFrames(ctx),
//        * stores the snapshot on the actor under __suspended_frame__,
//        * stores the awaited future on the actor under __waiting_on__,
//        * appends the actor to the awaited future's __waiters__ list so
//          the future's eventual resolve/reject can re-schedule it,
//        * rethrows so STRuntime::drainOne sees the yield.
//
//   3. STRuntime::drainOne catches FutureYield, marks the message as
//      yielded (the future is NOT resolved here — it stays pending) and
//      returns without re-scheduling the actor. The actor's mailbox lock
//      is released by the lock_guard scope.
//
//   4. When the awaited future is resolved/rejected (F6 v3 D), its
//      transition helper walks __waiters__ and schedules each waiting
//      actor. drainOne, on the next pop, sees __suspended_frame__ on the
//      actor and resumes by restoring the snapshot into a fresh engine
//      and pushing the resolved value (or rethrowing the rejection) into
//      the resumed frame's operand stack — the value the original
//      `wait` would have returned.
//
// The exception only carries the future pointer; the engine reads
// STRuntime::currentActor() at catch time for the "who yielded" half of the
// information.

namespace proto {
    class ProtoObject;
}

namespace protoST {

class FutureYield {
public:
    explicit FutureYield(const proto::ProtoObject* fut) noexcept : future_(fut) {}
    const proto::ProtoObject* future() const noexcept { return future_; }

private:
    const proto::ProtoObject* future_;
};

} // namespace protoST
