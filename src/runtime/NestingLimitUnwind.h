#pragma once
#include "protoCore.h"  // proto::proto_ulong

// S23: NestingLimitUnwind -- the C++ exception that carries the engine nesting
// limit's Error down to an `on:do:` whose handler could not start.
//
// A handler block runs on top of the `signal` that reached it, in a nested
// engine of its own. At the engine nesting limit (ExecutionEngine.cpp,
// kMaxNestedEngines) that engine cannot start. `signal` then raises the limit's
// documented Error ("stack depth exceeded (nested block evaluation)") in its
// place, finds the handler that catches it, and -- since that handler cannot
// start at this depth either -- throws NestingLimitUnwind{ handlerId }. The
// unwind leaves every activation above the target `on:do:` (running each
// ensure:/ifCurtailed: block on the way) and the `on:do:` that owns the id
// runs its handler there on a fresh instance of the limit Error, with the
// engines the unwind released. The handler's outcome is dispatched as for any
// signal: fall-through / return: answer the `on:do:`, retry re-evaluates the
// protected block, pass / outer / a new signal search outward.
//
// Why not a std::exception carrying the message: before S23 the limit was a
// bare std::runtime_error that left the handler's signal untranslated. Every
// primitive boundary it crossed on the way down translated it into a NEW
// Error and searched the handler stack again; a boundary inside an earlier
// handler of a re-signalling chain found the handlers above it enabled again
// (the unwind had already left their activations) and ran them a second time,
// each of which hit the limit again. The work doubled per level and a chain of
// ~500 re-signalling handlers did not finish. Carrying the target handler id,
// and not translating it again, delivers the limit Error exactly once.
//
// Like UnwindToHandler it is intentionally NOT a std::exception subclass, so
// the `catch (const std::exception&)` clauses of the engine and the
// primitives let it pass, and translateNativeException re-throws it untouched.
// A balanced `on:do:` always catches its own id; STRuntime::runTopLevel and
// drainOne report a stray one as an error.
//
// No ProtoObject travels with it: the Error instance is created at the
// `on:do:`, so nothing has to stay GC-reachable across the unwind.

namespace protoST {

class NestingLimitUnwind {
public:
    explicit NestingLimitUnwind(proto::proto_ulong handlerId) noexcept
        : handlerId_(handlerId) {}

    proto::proto_ulong handlerId() const noexcept { return handlerId_; }

private:
    proto::proto_ulong handlerId_;
};

} // namespace protoST
