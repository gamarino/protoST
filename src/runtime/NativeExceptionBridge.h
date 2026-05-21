#pragma once

// Track 1, slice 2 (EXC-d): native / UMD C++ exception translation.
//
// Every boundary where protoST calls into native code — a registered `prim_*`
// function, a UMD-provided module's loader — can throw a C++ exception. This
// header provides the two pieces that turn such an exception into a protoST
// `Error` catchable by an ordinary `on: Error do:` handler:
//
//   * `signalNativeError(rt, ctx, message)` — builds a fresh, NON-resumable
//     `Error` instance, sets its `messageText`, and runs it through the normal
//     `signalInstance` path. An active `on: Error do:` catches it; with no
//     handler it falls through to `defaultAction`, which throws
//     `UnhandledSTException`.
//
//   * `translateNativeException(rt, ctx, call)` — a template wrapper. It runs
//     the supplied native call and, on a C++ throw, decides what to do based
//     on the exception's type. The catch ORDER is load-bearing:
//
//       1. The control-flow siblings (NonLocalReturn, UnwindToHandler,
//          RetrySignal, ResumeSignal, PassSignal, FutureYield) are re-thrown
//          untouched — they are legitimate protoST control flow that a
//          primitive raises on purpose (e.g. `signal`/`return:`/`resume:`/
//          `retry`/`pass`, `Future>>wait`). None derives from std::exception.
//       2. `DebuggerHalt` is re-thrown untouched — the debugger's `halt`
//          primitive throws it on purpose; it DERIVES from std::runtime_error
//          so it MUST be caught before the generic std::exception clause.
//       3. `UnhandledSTException` is re-thrown untouched — it is already a
//          protoST exception escaping to the top (thrown by `defaultAction`).
//          Re-translating it would double-wrap. It too derives from
//          std::runtime_error, so it MUST precede the generic clause.
//       4. Any OTHER `std::exception` is translated via `signalNativeError`.
//       5. A non-std::exception throw becomes a generic native Error.
//
// `signalNativeError` itself can throw (`UnwindToHandler` if a handler caught
// the translated Error and did `return:`, `UnhandledSTException` if nothing
// caught it, etc.). Those propagate out of the wrapper naturally — which is
// correct: the translated Error then behaves exactly like any other signalled
// Error.

#include "runtime/NonLocalReturn.h"
#include "runtime/UnwindToHandler.h"
#include "runtime/RetrySignal.h"
#include "runtime/ResumeSignal.h"
#include "runtime/PassSignal.h"
#include "runtime/FutureYield.h"
#include "runtime/UnhandledSTException.h"
#include "debugger/DebuggerRuntime.h"

#include <exception>

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;

// Build a fresh non-resumable `Error` instance carrying `message` as its
// `messageText` and run it through the normal `signal` path. Defined in
// exception_prims.cpp next to `signalInstance`.
//
// May throw: `UnwindToHandler` (a handler caught it and did `return:` or fell
// through), `RetrySignal` (`retry`), or `UnhandledSTException` (no handler).
const proto::ProtoObject* signalNativeError(STRuntime& rt,
                                            proto::ProtoContext* ctx,
                                            const char* message);

// MNT-b2 (D3 / D8): signal a fresh, non-resumable instance of `errorClass`
// (`Error` or a subclass) carrying `message` as `messageText`, through the
// same `signalInstance` path as a script-level `signal`. Used by the engine
// to raise `MessageNotUnderstood` for an unknown selector and
// `BlockCannotReturn` for a dead-home non-local return — both then catchable
// by an ordinary `on: Error do:` handler. Defined in exception_prims.cpp.
//
// May throw the same control-flow exceptions as `signalNativeError`.
const proto::ProtoObject* signalErrorOfClass(STRuntime& rt,
                                             proto::ProtoContext* ctx,
                                             const proto::ProtoObject* errorClass,
                                             const char* message);

// Run `call` (a native / primitive / UMD invocation) and translate any C++
// exception it throws per the contract above. `Call` must be invocable with no
// arguments and return `const proto::ProtoObject*`.
template <typename Call>
const proto::ProtoObject* translateNativeException(STRuntime& rt,
                                                   proto::ProtoContext* ctx,
                                                   Call&& call) {
    try {
        return call();
    }
    // --- protoST control-flow siblings: re-throw untouched -----------------
    catch (const NonLocalReturn&)       { throw; }   // slice 1 — ^expr
    catch (const UnwindToHandler&)      { throw; }   // EXC — return:/fall-through
    catch (const RetrySignal&)          { throw; }   // EXC — retry
    catch (const ResumeSignal&)         { throw; }   // EXC — resume:
    catch (const PassSignal&)           { throw; }   // EXC — pass/outer
    catch (const FutureYield&)          { throw; }   // F6 v3 — cooperative yield
    // --- std::exception-DERIVED types that must NOT be translated ----------
    catch (const DebuggerHalt&)         { throw; }   // F2 — halt; is-a runtime_error
    catch (const UnhandledSTException&) { throw; }   // already protoST; is-a runtime_error
    // --- a genuine native error: translate into a catchable protoST Error --
    catch (const std::exception& e)     { return signalNativeError(rt, ctx, e.what()); }
    catch (...)                         { return signalNativeError(rt, ctx, "native exception"); }
}

} // namespace protoST
