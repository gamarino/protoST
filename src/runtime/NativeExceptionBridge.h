#pragma once

// Track 1, slice 2 (EXC-d): the native call boundary.
//
// Every boundary where protoST calls into native code -- a registered `prim_*`
// function, a protoCore method exported by a module (a call-form send), a
// UMD-provided module's loader -- can throw a C++ exception. This header
// provides the wrapper that turns what arrives there into a Smalltalk signal:
//
//   * `translateNativeException(rt, ctx, call)` runs the native call and, on a
//     C++ throw, decides what to do based on the exception's type. The catch
//     ORDER is load-bearing:
//
//       1. The control-flow siblings (NonLocalReturn, UnwindToHandler,
//          NestingLimitUnwind, RetrySignal, ResumeSignal, PassSignal,
//          FutureYield) are re-thrown untouched -- they are protoST control
//          flow a primitive raises on purpose (`^`, `return:`, `resume:`,
//          `retry`, `pass`, `Future>>wait`). None derives from std::exception
//          or proto::ProtoException.
//       2. `DebuggerHalt` and `InterruptSignal` are re-thrown untouched; they
//          derive from std::runtime_error, so they precede the generic clause.
//       3. `UnhandledSTException` is re-thrown untouched: it is a Smalltalk
//          exception every handler on this thread was already offered.
//       4. Any other `proto::ProtoException` -- protoCore's own error (built
//          by the runtime's error factory as ZeroDivide, InvalidArgument, ...),
//          an error type defined by a module, or a value raised by another
//          language -- is SIGNALLED with its value unchanged
//          (signalProtoException): a handler whose guard is on the value's
//          prototype chain runs, exactly as for `signal`. With no handler a
//          protoST exception value goes to its default action, and a foreign
//          value leaves as the very same ProtoException.
//       5. `ClassedErrorSignal` (a kernel Error subclass by name) and any
//          other `std::exception` -- a C++ library failure, a module defect --
//          are translated into a non-resumable protoST Error whose
//          messageText is `what()`.
//       6. Anything else becomes a generic native Error.
//
// The signal paths can themselves throw (`UnwindToHandler` when a handler did
// `return:` or fell through, `RetrySignal` for `retry`, `UnhandledSTException`
// or the original ProtoException when nothing handled it). Those propagate out
// of the wrapper, which is correct: the native error then behaves exactly like
// any other signalled exception.

#include "runtime/NonLocalReturn.h"
#include "runtime/UnwindToHandler.h"
#include "runtime/NestingLimitUnwind.h"
#include "runtime/RetrySignal.h"
#include "runtime/ResumeSignal.h"
#include "runtime/PassSignal.h"
#include "runtime/FutureYield.h"
#include "runtime/UnhandledSTException.h"
#include "runtime/Interrupt.h"
#include "runtime/ClassedErrorSignal.h"
#include "debugger/DebuggerRuntime.h"
#include "protoCore.h"

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
// Signal `value`, the value of the ProtoException held by `caught`, which
// keeps it pinned (exception_prims.cpp). A protoST exception instance is
// signalled like `signal`; any other value is signalled as a foreign,
// non-resumable exception, and when no handler takes it the original
// exception is re-thrown with its value unchanged.
const proto::ProtoObject* signalProtoException(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* value,
                                               std::exception_ptr caught);

// The Exception protocol for a foreign exception while its handler runs
// (exception_prims.cpp): whether `selector` sent to `recv` is answered by
// Error's protocol, and the send itself. Used by the engine's
// doesNotUnderstand path, so ordinary sends pay nothing for it.
bool canSendToForeignException(STRuntime& rt, proto::ProtoContext* ctx,
                               const proto::ProtoObject* recv,
                               const proto::ProtoString* selector);
const proto::ProtoObject* sendToForeignException(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* recv,
                                                 const proto::ProtoString* selector,
                                                 const proto::ProtoObject* const* args, int argc);

// The text of a value used as an exception: its messageText, or the `name` /
// `message` of a foreign one (exception_prims.cpp).
std::string describeExceptionValue(proto::ProtoContext* ctx, const proto::ProtoObject* value);

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
    // do: blocks), grew the native stack until it overflowed. A signal runs
    // after the try statement for the same reason: its handler runs on top.
    std::exception_ptr untouched;
    std::exception_ptr signalled;
    const proto::ProtoObject* signalledValue = nullptr;   // pinned by `signalled`
    try {
        return call();
    }
    // --- protoST control-flow siblings: re-throw untouched -----------------
    catch (const NonLocalReturn&)       { untouched = std::current_exception(); }   // slice 1 — ^expr
    catch (const UnwindToHandler&)      { untouched = std::current_exception(); }   // EXC — return:/fall-through
    catch (const NestingLimitUnwind&)   { untouched = std::current_exception(); }   // S23 — limit Error for an on:do:
    catch (const RetrySignal&)          { untouched = std::current_exception(); }   // EXC — retry
    catch (const ResumeSignal&)         { untouched = std::current_exception(); }   // EXC — resume:
    catch (const PassSignal&)           { untouched = std::current_exception(); }   // EXC — pass/outer
    catch (const FutureYield&)          { untouched = std::current_exception(); }   // F6 v3 — cooperative yield
    // --- std::exception-DERIVED types that must NOT be translated ----------
    catch (const DebuggerHalt&)         { untouched = std::current_exception(); }   // F2 — halt; is-a runtime_error
    catch (const InterruptSignal&)      { untouched = std::current_exception(); }   // Ctrl-C in the REPL; is-a runtime_error
    // --- language values (protoCore 3.0.0 unified exceptions) -------------
    catch (const UnhandledSTException&) { untouched = std::current_exception(); }   // already offered to every handler
    catch (const proto::ProtoException& e) {                                        // protoCore / module / other language
        signalledValue = e.value();
        signalled = std::current_exception();
    }
    // --- a genuine native error: translate into a catchable protoST Error --
    catch (const ClassedErrorSignal& e) { return signalErrorNamed(rt, ctx, e.className(), e.what()); }
    catch (const std::exception& e)     { return signalNativeError(rt, ctx, e.what()); }
    catch (...)                         { return signalNativeError(rt, ctx, "native exception"); }
    if (signalled) return signalProtoException(rt, ctx, signalledValue, std::move(signalled));
    std::rethrow_exception(untouched);
}

} // namespace protoST
