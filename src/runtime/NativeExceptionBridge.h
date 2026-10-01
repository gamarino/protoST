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
#include "runtime/Interrupt.h"
#include "runtime/ZeroDivideSignal.h"
#include "debugger/DebuggerRuntime.h"

#include <exception>
#include <string>

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
// Signal a fresh ZeroDivide (defined in exception_prims.cpp).
const proto::ProtoObject* signalZeroDivide(STRuntime& rt, proto::ProtoContext* ctx);

// Signal a fresh instance of the global Error subclass named `className`
// (a plain Error if there is none), carrying `message` (exception_prims.cpp).
const proto::ProtoObject* signalErrorNamed(STRuntime& rt, proto::ProtoContext* ctx,
                                           const char* className, const char* message);
bool isExceptionClassObject(STRuntime& rt, proto::ProtoContext* ctx,
                            const proto::ProtoObject* obj);

// Build a Message (selector + arguments) and signal a resumable
// MessageNotUnderstood carrying it and the receiver (exception_prims.cpp).
const proto::ProtoObject* makeMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                      const std::string& selector,
                                      const proto::ProtoObject* const* args, int argc);
const proto::ProtoObject* signalMessageNotUnderstood(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* receiver,
                                                     const proto::ProtoObject* message,
                                                     const char* text);

// Re-signal an existing exception instance in the current context, and test
// whether an object is one (exception_prims.cpp).
const proto::ProtoObject* resignalException(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* exc);
bool isExceptionInstance(STRuntime& rt, proto::ProtoContext* ctx,
                         const proto::ProtoObject* obj);

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
    // "Re-throw untouched" below means: keep the exception and re-throw it
    // after the try statement, not with `throw;` inside the catch clause.
    // Same meaning under the Itanium ABI; under MSVC a catch clause runs
    // before the frames below it are released, so a re-throw inside it, at
    // every primitive boundary an unwind crosses (a deep recursion through
    // do: blocks), grew the native stack until it overflowed.
    std::exception_ptr untouched;
    try {
        return call();
    }
    // --- protoST control-flow siblings: re-throw untouched -----------------
    catch (const NonLocalReturn&)       { untouched = std::current_exception(); }   // slice 1 — ^expr
    catch (const UnwindToHandler&)      { untouched = std::current_exception(); }   // EXC — return:/fall-through
    catch (const RetrySignal&)          { untouched = std::current_exception(); }   // EXC — retry
    catch (const ResumeSignal&)         { untouched = std::current_exception(); }   // EXC — resume:
    catch (const PassSignal&)           { untouched = std::current_exception(); }   // EXC — pass/outer
    catch (const FutureYield&)          { untouched = std::current_exception(); }   // F6 v3 — cooperative yield
    // --- std::exception-DERIVED types that must NOT be translated ----------
    catch (const DebuggerHalt&)         { untouched = std::current_exception(); }   // F2 — halt; is-a runtime_error
    catch (const InterruptSignal&)      { untouched = std::current_exception(); }   // Ctrl-C in the REPL; is-a runtime_error
    catch (const UnhandledSTException&) { untouched = std::current_exception(); }   // already protoST; is-a runtime_error
    catch (const ZeroDivideSignal&)     { return signalZeroDivide(rt, ctx); }
    catch (const ClassedErrorSignal& e) { return signalErrorNamed(rt, ctx, e.className(), e.what()); }
    // --- a genuine native error: translate into a catchable protoST Error --
    catch (const std::exception& e)     { return signalNativeError(rt, ctx, e.what()); }
    catch (...)                         { return signalNativeError(rt, ctx, "native exception"); }
    std::rethrow_exception(untouched);
}

} // namespace protoST
