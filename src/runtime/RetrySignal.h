#pragma once

// Track 1, slice 2 (EXC-b): RetrySignal — the C++ exception that carries a
// handler's `retry` decision back to the owning `on:do:` activation.
//
// Control-flow contract:
//
//   `anException retry` throws RetrySignal{ activeHandlerId }. Unlike
//   ResumeSignal, the RetrySignal is NOT caught by `signal`'s handler-running
//   loop — it propagates PAST `signal` up to the `on:do:` primitive, which
//   catches it for its own handlerId and re-evaluates the protected block
//   from scratch (a fresh handler id is pushed for the retried attempt).
//
//   A RetrySignal whose handlerId is not this `on:do:`'s is re-thrown for an
//   outer `on:do:`. A RetrySignal that escapes a whole actor message /
//   top-level run is a bug, converted to a runtime error in STRuntime.
//
//   Sibling of UnwindToHandler / ResumeSignal / PassSignal — not a
//   std::exception subclass.

namespace protoST {

class RetrySignal {
public:
    explicit RetrySignal(unsigned long handlerId) noexcept
        : handlerId_(handlerId) {}

    unsigned long handlerId() const noexcept { return handlerId_; }

private:
    unsigned long handlerId_;
};

} // namespace protoST
