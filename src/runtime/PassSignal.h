#pragma once

// Track 1, slice 2 (EXC-b): PassSignal — the C++ exception that carries a
// handler's `pass` (a.k.a. `outer`) decision back to `signal`'s loop.
//
// Control-flow contract:
//
//   `anException pass` throws PassSignal{ activeHandlerId }. The handler runs
//   IN PLACE inside `signal`'s `invokeBlock`; the PassSignal propagates out of
//   that nested engine up to `signal`'s handler-running loop, which catches it
//   for its own handlerId and RESUMES the handler search OUTWARD from just
//   beyond the current entry. If an outer handler matches, it runs; if none
//   remains, the exception's default action runs.
//
//   For this MVP `outer` is bound as an alias of `pass`. Strict Smalltalk
//   `outer` evaluates the outer handler and, if it resumes, returns control to
//   the inner handler; that round-trip is a deliberate, documented
//   simplification deferred beyond EXC-b.
//
//   A PassSignal whose handlerId is not the current loop's is re-thrown so an
//   outer `signal` loop catches it. A PassSignal that escapes a whole actor
//   message / top-level run is a bug, converted to a runtime error in
//   STRuntime.
//
//   Sibling of UnwindToHandler / ResumeSignal / RetrySignal — not a
//   std::exception subclass.

namespace protoST {

class PassSignal {
public:
    explicit PassSignal(unsigned long handlerId) noexcept
        : handlerId_(handlerId) {}

    unsigned long handlerId() const noexcept { return handlerId_; }

private:
    unsigned long handlerId_;
};

} // namespace protoST
