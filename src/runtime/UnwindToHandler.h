#pragma once

// Track 1, slice 2 (EXC-a): UnwindToHandler — the C++ exception that carries
// the result of an exception handler back to its `on:do:` activation.
//
// Control-flow contract:
//
//   1. `signal` (signal / signal:) does NOT throw to FIND a handler. It walks
//      the runtime's thread-local handler stack (HandlerStack.h) with the
//      signalling stack intact, runs the matching handler block IN PLACE, and
//      only then — once the handler has decided to unwind — throws
//      UnwindToHandler{ handlerId, value } to abandon the protected
//      computation and return `value` from the owning `on:do:`.
//
//   2. The two EXC-a unwind triggers are:
//        * the handler block falls off its end  → value = its last value;
//        * the handler does `anException return: v` → value = v.
//      Both throw UnwindToHandler carrying the handlerId stamped onto the
//      exception instance at signal time, so the throw targets exactly the
//      `on:do:` whose guard matched.
//
//   3. UnwindToHandler is intentionally NOT a std::exception subclass — like
//      its siblings NonLocalReturn (slice 1) and FutureYield (F6 v3). The
//      nested-engine call sites (invokeBlock, primitives) and STRuntime paths
//      catch `const std::exception&` for ordinary error handling; an
//      UnwindToHandler must slip past those and bubble to the `on:do:`
//      primitive, which catches it explicitly and checks `handlerId`.
//
//   4. ExecutionEngine::runLoop has only typed catches (NonLocalReturn,
//      FutureYield, DebuggerHalt); UnwindToHandler matches none of them and
//      propagates untouched, exactly as required. STRuntime::drainOne and
//      runTopLevel add an explicit `catch (const UnwindToHandler&)` purely as
//      a defensive fallback: a balanced `on:do:` always catches its own id,
//      so a stray UnwindToHandler escaping a whole actor message / top-level
//      run is a bug — it is converted to a clear error rather than allowed to
//      call std::terminate.

namespace proto {
    class ProtoObject;
}

namespace protoST {

class UnwindToHandler {
public:
    UnwindToHandler(unsigned long handlerId,
                    const proto::ProtoObject* value) noexcept
        : handlerId_(handlerId), value_(value) {}

    unsigned long handlerId() const noexcept { return handlerId_; }
    const proto::ProtoObject* value() const noexcept { return value_; }

private:
    unsigned long             handlerId_;
    const proto::ProtoObject*  value_;
};

} // namespace protoST
