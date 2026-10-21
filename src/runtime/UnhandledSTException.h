#pragma once

// Track 1, slice 2 (EXC-d): UnhandledSTException — the C++ exception thrown by
// the exception protocol's `defaultAction` when a protoST exception reaches no
// handler.
//
// protoCore 3.0.0 (unified exceptions): it is a proto::ProtoException carrying
// the exception object itself (its `value()`), so an unhandled Smalltalk Error
// leaves the runtime -- to a C++ embedder, or to another language on
// protoCore -- as the one exception type every language catches, and a
// handler there selects it by its prototype chain (`isInstanceOf(ctx, Error)`).
// protoCore pins the value while the exception is in flight; that replaces
// the `__inflight_exception__` actor attribute earlier versions kept the
// exception reachable through.
//
// Why a dedicated subclass. Every native/primitive call boundary
// (translateNativeException, NativeExceptionBridge.h) signals a
// ProtoException it catches as a Smalltalk exception, so that protoCore's
// errors and the errors of a module are caught by `on:do:`. An
// UnhandledSTException has already been through that search -- every handler
// on the thread's handler stack was offered it -- so the boundary recognises
// this type and re-throws it untouched instead of signalling it again. The
// subclass also keeps two extras the top level prints: the description the
// default action computed (`message()`, also `what()`) and the active stack.
//
// It is NOT a std::exception (proto::ProtoException deliberately is not):
// code that catches `const std::exception&` for library failures does not
// see it. The top-level entry points catch `const proto::ProtoException&`
// and report it with describeUncaught.

#include "protoCore.h"

#include <exception>
#include <string>
#include <utility>

namespace protoST {

class UnhandledSTException : public proto::ProtoException {
public:
    UnhandledSTException(proto::ProtoContext* ctx, const proto::ProtoObject* exception,
                         std::string message, std::string trace = std::string(),
                         bool actorException = false,
                         std::exception_ptr original = nullptr)
        : proto::ProtoException(ctx, exception), message_(std::move(message)),
          trace_(std::move(trace)), actorException_(actorException),
          original_(std::move(original)) {}

    // The description of the exception, as the default action computed it
    // when the exception went unhandled ("AppError: too much").
    const std::string& message() const noexcept { return message_; }
    const char* what() const noexcept { return message_.c_str(); }
    // The active methods when the error went unhandled, innermost first, one
    // "  at Class>>selector (file:line)" line each; empty when unknown. Kept
    // apart from message(), which handlers and rejected Futures see.
    const std::string& trace() const noexcept { return trace_; }
    // True when the exception went unhandled inside an actor method, so the
    // drain rejects that message's Future with the exception (value()).
    bool actorException() const noexcept { return actorException_; }
    // For a foreign exception (a module's error, another language's
    // exception) that arrived as a ProtoException and no handler took: that
    // original exception. Inside the runtime the foreign value travels in
    // this UnhandledSTException, so the native boundaries it crosses on the
    // way out do not offer it to the handlers again -- a handler re-enabled
    // when the handler block the error escaped from ended would otherwise
    // run for an error raised inside its own handler. Where the exception
    // leaves the runtime (STRuntime::runTopLevel, a module load for another
    // runtime) the original is re-thrown, so the caller receives the very
    // ProtoException the module raised, value and C++ type unchanged.
    const std::exception_ptr& original() const noexcept { return original_; }

private:
    std::string message_;
    std::string trace_;
    bool actorException_;
    std::exception_ptr original_;
};

// "error: <message>" for a C++ failure that reached a top level.
std::string describeUncaught(const std::exception& e);

// "error: <description>" followed by the trace for an uncaught
// ProtoException: an UnhandledSTException's message and trace, or, for a
// value raised by a module or another language, its `name` and `message`
// attributes ("NegativeRadius: circleArea: the radius ... is negative").
// `ctx` reads the value's attributes; the exception's space must be alive.
// Defined in exception_prims.cpp.
std::string describeUncaught(proto::ProtoContext* ctx, const proto::ProtoException& e);

// The description alone, without the "error: " prefix and the trace.
std::string describeProtoException(proto::ProtoContext* ctx, const proto::ProtoException& e);

} // namespace protoST
