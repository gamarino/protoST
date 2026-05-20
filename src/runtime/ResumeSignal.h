#pragma once

// Track 1, slice 2 (EXC-b): ResumeSignal — the C++ exception that carries a
// handler's `resume:` decision back to the `signal` that invoked the handler.
//
// Control-flow contract:
//
//   `anException resume: v` throws ResumeSignal{ activeHandlerId, v }. The
//   handler block runs IN PLACE inside `signal`'s `invokeBlock`; the
//   ResumeSignal propagates out of that nested engine — it is NOT a
//   std::exception, so ordinary `catch (const std::exception&)` ignores it —
//   up to `signal`'s handler-running loop. That loop catches the ResumeSignal
//   for its own handlerId and RETURNS `v`: the `signal` primitive call, back
//   in the protected block whose stack was never unwound, yields `v` and the
//   computation continues. This is the whole point of running handlers in
//   place — no unwinding, the signalling stack stays intact.
//
//   A ResumeSignal whose handlerId is not the current loop's is re-thrown so
//   an outer `signal` loop catches it. A ResumeSignal that escapes a whole
//   actor message / top-level run is a bug, converted to a runtime error in
//   STRuntime::drainOne / runTopLevel — never allowed to call std::terminate.
//
//   Sibling of UnwindToHandler / RetrySignal / PassSignal / NonLocalReturn /
//   FutureYield — none derive from std::exception.

namespace proto {
    class ProtoObject;
}

namespace protoST {

class ResumeSignal {
public:
    ResumeSignal(unsigned long handlerId,
                 const proto::ProtoObject* value) noexcept
        : handlerId_(handlerId), value_(value) {}

    unsigned long handlerId() const noexcept { return handlerId_; }
    const proto::ProtoObject* value() const noexcept { return value_; }

private:
    unsigned long             handlerId_;
    const proto::ProtoObject* value_;
};

} // namespace protoST
