#pragma once
#include "protoCore.h"  // proto::proto_ulong
#include "runtime/PinnedValue.h"

// Track 1, slice 2 (EXC-b): RetrySignal — the C++ exception that carries a
// handler's `retry` / `retryUsing:` decision back to the owning `on:do:`
// activation.
//
// Control-flow contract:
//
//   `anException retry` throws RetrySignal{ activeHandlerId }. Unlike
//   ResumeSignal, the RetrySignal is NOT caught by `signal`'s handler-running
//   loop — it propagates PAST `signal` up to the `on:do:` primitive, which
//   catches it for its own handlerId and re-evaluates the protected block
//   from scratch (a fresh handler id is pushed for the retried attempt).
//
//   `anException retryUsing: aBlock` throws the same signal carrying aBlock as
//   its replacement: the `on:do:` makes aBlock its protected block and
//   evaluates it under the same handlers, as Pharo's
//   `handlerContext restartWithNewReceiver:` does. A later `retry` evaluates
//   the replacement again.
//
//   A RetrySignal whose handlerId is not this `on:do:`'s is re-thrown for an
//   outer `on:do:`. A RetrySignal that escapes a whole actor message /
//   top-level run is a bug, converted to a runtime error in STRuntime.
//
//   Sibling of UnwindToHandler / ResumeSignal — not a std::exception
//   subclass.

namespace protoST {

class RetrySignal {
public:
    // `replacement` (nullptr for a plain retry) is pinned while the signal is
    // in flight to the on:do: (PinnedValue.h).
    RetrySignal(proto::ProtoContext* ctx, proto::proto_ulong handlerId,
                const proto::ProtoObject* replacement = nullptr)
        : handlerId_(handlerId), replacement_(ctx, replacement) {}

    proto::proto_ulong handlerId() const noexcept { return handlerId_; }
    // The replacement protected block, or nullptr for a plain retry.
    const proto::ProtoObject* replacement() const noexcept {
        const proto::ProtoObject* r = replacement_.get();
        return r == PROTO_NONE ? nullptr : r;
    }

private:
    proto::proto_ulong handlerId_;
    PinnedValue        replacement_;
};

} // namespace protoST
