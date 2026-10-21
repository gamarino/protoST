// Track 1, slice 2 (EXC-a + EXC-b): the exception protocol primitives.
//
// Implements the class hierarchy accessors, `signal` / `signal:`, the
// protected-block primitives `on:do:` / `on:do:on:do:`, and the handler
// actions `return:`, `resume:`, `retry`, `pass` / `outer`. `ensure:` /
// `ifCurtailed:` are EXC-c; native C++ exception translation is EXC-d.
//
// Control-flow summary:
//
//   * `on:do:` / `on:do:on:do:` (on blockProto) push one or more
//     HandlerEntries, run the protected block via invokeBlock, and pop the
//     entries on EVERY exit path. Each owns a unique handlerId; an
//     UnwindToHandler carrying any of those ids is this construct's signal to
//     return the carried value. A RetrySignal carrying one of those ids
//     re-evaluates the protected block from scratch.
//
//   * `signal` runs a LOOP over the thread-local handler stack. Each turn it
//     finds the newest enabled matching entry, records the handler id on the
//     thread's active-signal stack, disables that entry plus every inner one,
//     and runs the handler block IN PLACE. The handler outcome is then dispatched:
//       - fall-through / `return:` → throw UnwindToHandler (unwind the
//         protected block, the `on:do:` yields the value);
//       - `resume:` (ResumeSignal) → caught here; `signal` RETURNS the value,
//         the protected block continues from the signal point;
//       - `pass` / `outer` (PassSignal) → caught here; the loop searches
//         OUTWARD for the next matching handler;
//       - `retry` (RetrySignal) → NOT caught here; propagates to `on:do:`.
//     With NO matching handler the exception's default action runs (EXC-b
//     refinement: a resumable Exception/Warning resumes with nil; an Error /
//     non-resumable one aborts by throwing UnhandledSTException, a
//     proto::ProtoException carrying the exception).
//
//   * protoCore 3.0.0 (unified exceptions): a proto::ProtoException that
//     reaches a native boundary is signalled through the same loop with its
//     value unchanged (signalProtoException). A value that is not a protoST
//     exception -- an error type defined by a module, or another language's
//     exception -- is a FOREIGN exception: it is matched by its prototype
//     chain like any other, and by `on: Exception do:` (the one catch-all
//     form; `on: Error do:` does not select it), it is never resumable, the Exception protocol
//     (messageText, return:, retry, pass, ...) answers for it while its
//     handler runs, and when no handler takes it the original ProtoException
//     continues outward. protoCore's own errors arrive already built as
//     protoST exceptions by the space's error factory (stErrorFactory).

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/HandlerStack.h"
#include "runtime/UnwindToHandler.h"
#include "runtime/NestingLimitUnwind.h"
#include "runtime/ResumeSignal.h"
#include "runtime/RetrySignal.h"
#include "runtime/PassSignal.h"
#include "runtime/FutureYield.h"
#include "runtime/UnhandledSTException.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/TransientPin.h"
#include "runtime/ExecutionEngine.h"
#include "modules/STModuleProvider.h"
#include "protoCore.h"

#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

int blockArgCount(proto::ProtoContext* ctx, const proto::ProtoObject* block);

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);
// The same, with the method looked up on `lookupRoot` instead of the receiver.
const proto::ProtoObject* sendDynamicFrom(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* recv,
                                          const proto::ProtoObject* lookupRoot,
                                          const proto::ProtoString* selector,
                                          const proto::ProtoObject* const* args, int argc,
                                          bool* understood);

// invokeBlock — defined in block_prims.cpp. Runs a BlockClosure synchronously
// in a fresh nested ExecutionEngine. An UnwindToHandler thrown inside the
// block (or inside `signal` which the block called) is NOT a std::exception,
// so it bubbles straight through invokeBlock's nested engine untouched.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc);

namespace {

// Attribute keys. Earlier this file called createSymbol on every
// invocation, citing concern that a function-local static would
// "dangle for every later STRuntime".  Two facts make the static
// cache safe:
//   (1) protoST documents one-runtime-per-process as a hard edge
//       (KNOWN_ISSUES K1) — a second STRuntime is not a supported
//       configuration.
//   (2) createSymbol allocates via the perpetual path
//       (is_strong=true) — the resulting pointer lives for the
//       process and is never reclaimed by the GC.  Even if multiple
//       ProtoSpaces existed simultaneously, the cached pointer would
//       remain valid; it would just always resolve to the first
//       space's symbol, which is fine for attribute-key identity.
// Under exception_latency (50K iterations × signal+on:do:) the
// uncached version showed ~4-8 createSymbol calls per iteration and
// 2.05% of cycles in toUTF8String — exactly the WKS-string
// canonicalisation that the JSSymbols / call_once pattern in protoJS
// was built to avoid.
const proto::ProtoString* msgTextKey(proto::ProtoContext* ctx) {
    // Resolved per call: symbols are interned per ProtoSpace, and a static
    // would bind to the first runtime's space.
    return proto::ProtoString::createSymbol(ctx, "__message_text__");
}
const proto::ProtoString* classNameKey(proto::ProtoContext* ctx) {
    // Resolved per call: symbols are interned per ProtoSpace, and a static
    // would bind to the first runtime's space.
    return proto::ProtoString::createSymbol(ctx, "__class_name__");
}
const proto::ProtoString* resumableKey(proto::ProtoContext* ctx) {
    // Resolved per call: symbols are interned per ProtoSpace, and a static
    // would bind to the first runtime's space.
    return proto::ProtoString::createSymbol(ctx, "__resumable__");
}

// --- The active-signal stack ----------------------------------------------
//
// One entry per handler block currently running on this thread, innermost
// last: the exception it handles, the id of its handler entry, and whether the
// exception is foreign (a value raised by a module or another language, not a
// protoST exception instance). The handler actions (`return:`, `resume:`,
// `retry`, `pass`), which only receive the exception as their receiver, find
// their handler here by the exception's identity.
//
// Earlier versions stamped the id onto the instance as an attribute. That
// cannot work for a foreign exception: its value is not protoST's to modify,
// and an immutable one cannot be modified at all. The stack also restores the
// outer id when a handler re-signals the same instance, which the stamp
// overwrote. Thread-local, as the handler stack is: a handler runs on the
// thread that signalled. The exception is pinned by the signalInstance that
// pushed the entry.
struct ActiveSignal {
    const proto::ProtoObject* exception;
    proto::proto_ulong handlerId;
    bool foreign;
};
thread_local std::vector<ActiveSignal> g_activeSignals;

struct ActiveSignalScope {
    ActiveSignalScope(const proto::ProtoObject* exc, proto::proto_ulong id, bool foreign) {
        g_activeSignals.push_back(ActiveSignal{exc, id, foreign});
    }
    ~ActiveSignalScope() { g_activeSignals.pop_back(); }
    ActiveSignalScope(const ActiveSignalScope&) = delete;
    ActiveSignalScope& operator=(const ActiveSignalScope&) = delete;
};

const ActiveSignal* findActiveSignal(const proto::ProtoObject* exc) {
    for (std::size_t i = g_activeSignals.size(); i-- > 0; )
        if (g_activeSignals[i].exception == exc) return &g_activeSignals[i];
    return nullptr;
}

// True when the exception instance is resumable. The `__resumable__` marker is
// installed class-side at bootstrap (`Exception`/`Warning` true, `Error`
// false) and inherited by instances and user subclasses; an instance may
// override it. Absent marker is treated as resumable (the `Exception` root
// default) — only `Error` and its descendants stamp `false`. A foreign
// exception is never resumable: the native frames between its origin and the
// boundary that signalled it are gone.
bool isResumable(proto::ProtoContext* ctx, const proto::ProtoObject* exc) {
    if (!exc) return false;
    if (const ActiveSignal* active = findActiveSignal(exc))
        if (active->foreign) return false;
    const proto::ProtoObject* m = exc->getAttribute(ctx, resumableKey(ctx));
    if (!m || m == PROTO_NONE) return true;
    return m == PROTO_TRUE;
}

// The id of the handler currently running for `exc`, or 0 when the exception
// is not currently being handled on this thread.
proto::proto_ulong activeHandlerIdOf(proto::ProtoContext*, const proto::ProtoObject* exc) {
    const ActiveSignal* active = exc ? findActiveSignal(exc) : nullptr;
    return active ? active->handlerId : 0;
}

// A string attribute of `v`, or "" when absent or not a string.
std::string stringAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                            const char* name) {
    if (!v || v == PROTO_NONE || !v->isCell(ctx) || v->asString(ctx)) return std::string();
    const proto::ProtoObject* a = v->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
    if (!a || a == PROTO_NONE || !a->isString(ctx)) return std::string();
    const proto::ProtoString* s = a->asString(ctx);
    return s ? s->toStdString(ctx) : std::string();
}

// True when `obj` is a class object (carries `__class_name__` as an OWN
// attribute). Class-side `signal` / `signal:` is sent to such an object; an
// instance does not own `__class_name__` — it inherits it.
bool isClassObject(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return false;
    const proto::ProtoObject* own = obj->getOwnAttributeDirect(ctx, classNameKey(ctx));
    return own && own != PROTO_NONE;
}

// The exception instance's messageText, or "" when nil/absent.
std::string messageTextOf(proto::ProtoContext* ctx, const proto::ProtoObject* exc) {
    const proto::ProtoObject* m = exc ? exc->getAttribute(ctx, msgTextKey(ctx)) : nullptr;
    if (!m || m == PROTO_NONE) return std::string();
    const proto::ProtoString* s = m->asString(ctx);
    return s ? s->toStdString(ctx) : std::string();
}

// The text an unhandled exception reports: its messageText as the exception
// answers it (so a messageText override counts), prefixed with the class name
// when that adds information — "MyError: text", but "text" for a plain Error
// and for MessageNotUnderstood, whose text already names the selector and the
// receiver's class.
std::string defaultActionMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                 const proto::ProtoObject* exc) {
    std::string className;
    const proto::ProtoObject* cn = exc ? exc->getAttribute(ctx, classNameKey(ctx)) : nullptr;
    if (cn && cn != PROTO_NONE)
        if (const proto::ProtoString* s = cn->asString(ctx)) className = s->toStdString(ctx);
    std::string text;
    try {
        bool understood = false;
        const proto::ProtoObject* m = sendDynamic(
            rt, ctx, exc, proto::ProtoString::createSymbol(ctx, "messageText"),
            nullptr, 0, &understood);
        if (understood && m && m != PROTO_NONE)
            if (const proto::ProtoString* s = m->asString(ctx)) text = s->toStdString(ctx);
    } catch (...) {
        text.clear();   // a failing override: fall back to the stored text
    }
    if (text.empty()) text = messageTextOf(ctx, exc);
    if (text.empty()) return className.empty() ? std::string("unhandled exception") : className;
    if (className.empty() || className == "Error" || className == "MessageNotUnderstood"
        || text == className || text.rfind(className + ":", 0) == 0)
        return text;
    return className + ": " + text;
}

// --- Default action for an unhandled exception (EXC-b refinement) ----------
//
// Runs when the handler search exhausts the stack with no match. The outcome
// depends on whether the exception is resumable:
//   * resumable (Exception base, Warning) → for a Warning, print its
//     messageText to stderr; then RESUME the signal with nil so the
//     computation continues. `signal` returns the result of this function.
//   * non-resumable (Error and descendants) → abort the activation by
//     throwing UnhandledSTException — an actor rejects its Future, the REPL/-e
//     prints it. This function does not return in that case.
//
// EXC-d: the abort throw is UnhandledSTException, a proto::ProtoException
// carrying the exception (protoCore 3.0.0): it leaves the runtime as the
// exception type every protoCore language catches, and the dedicated type lets
// the native boundary recognise an exception every handler was already offered
// and re-throw it instead of signalling it again (UnhandledSTException.h).
const proto::ProtoObject* defaultAction(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* exc) {
    const bool isErrorClass = exc && exc->getAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "__unhandled_is_error__")) == PROTO_TRUE;
    if (isErrorClass || !isResumable(ctx, exc)) {
        // Error (resumable or not) / non-resumable: abort the activation by
        // throwing the exception itself, as a ProtoException (protoCore pins
        // it while it is in flight). Inside an actor the drain rejects the
        // message's Future with it.
        const bool onActor = rt.currentActor() != nullptr;
        throw UnhandledSTException(ctx, exc, defaultActionMessage(rt, ctx, exc),
                                   ExecutionEngine::describeActiveStack(20), onActor);
    }
    // Resumable and unhandled. A Warning announces itself; the bare Exception
    // base resumes silently. The distinction is the presence of a messageText
    // worth reporting — print it for any resumable exception that carries one.
    {
        std::string m = messageTextOf(ctx, exc);
        if (!m.empty()) {
            // Labelled with the exception's class (a Warning, a user
            // subclass, or the bare Exception), never a fixed word.
            std::string label = "Exception";
            const proto::ProtoObject* cn = exc->getAttribute(ctx, classNameKey(ctx));
            if (cn && cn != PROTO_NONE)
                if (const proto::ProtoString* ns = cn->asString(ctx)) label = ns->toStdString(ctx);
            std::fputs((label + ": ").c_str(), stderr);
            std::fputs(m.c_str(), stderr);
            std::fputc('\n', stderr);
        }
    }
    // Resume with nil: `signal` returns nil, the computation continues.
    return PROTO_NONE;
}

// --- Core: signal an exception INSTANCE ------------------------------------
//
// Shared by instance-side `signal` and class-side `signal` / `signal:`.
//
// EXC-b restructures the handler-running as a LOOP so `pass` can move the
// search outward. Each turn finds the newest enabled matching entry, runs its
// handler block in place, and dispatches on the outcome:
//   * fall-through → throw UnwindToHandler (the `on:do:` yields the value);
//   * ResumeSignal for this id → return the value (the protected block
//     continues from the signal point);
//   * PassSignal for this id → continue the loop, searching strictly outward;
//   * RetrySignal / UnwindToHandler / anything else → propagate past `signal`
//     (RetrySignal reaches `on:do:`; a foreign throw bubbles on).
//
// S23: a handler block needs a nested engine of its own. At the engine
// nesting limit it cannot start, and the limit's Error is raised in its place
// (raiseNestingLimitError): it unwinds to the `on:do:` whose handler catches
// it, which then calls this function again with `firstHandlerId` set to run
// that handler on the limit Error. 0 == search the handler stack as usual.
//
// `origin` is set when `exc` is the value of a ProtoException caught at a
// native boundary (signalProtoException): a foreign exception no handler
// takes then re-throws that very exception.
const proto::ProtoObject* signalInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exc,
                                          proto::proto_ulong firstHandlerId = 0,
                                          const std::exception_ptr* origin = nullptr);

// The text a foreign exception reports: "<name>: <message>" from its `name`
// and `message` attributes, the convention protoCore's own error values and
// its example modules follow.
std::string describeForeignValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const std::string name = stringAttribute(ctx, v, "name");
    const std::string message = stringAttribute(ctx, v, "message");
    if (!name.empty() && !message.empty()) return name + ": " + message;
    if (!message.empty()) return message;
    if (!name.empty()) return name;
    return "a foreign exception";
}

// The default action of a foreign exception no handler took: it continues
// outward with its value unchanged, as an UnhandledSTException, so no outer
// native boundary offers it to the handlers again. It carries the
// ProtoException it arrived in, when there is one, which is what leaves the
// runtime (UnhandledSTException::original); a re-signalled value (`e signal`,
// a rejected Future) leaves as the UnhandledSTException itself.
[[noreturn]] void foreignDefaultAction(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* exc,
                                       const std::exception_ptr* origin) {
    throw UnhandledSTException(ctx, exc, describeForeignValue(ctx, exc),
                               ExecutionEngine::describeActiveStack(20),
                               rt.currentActor() != nullptr,
                               origin ? *origin : std::exception_ptr());
}

// A fresh, non-resumable Error carrying the engine nesting limit's message.
const proto::ProtoObject* newNestingLimitError(STRuntime& rt, proto::ProtoContext* ctx) {
    const proto::ProtoObject* exc =
        const_cast<proto::ProtoObject*>(rt.bootstrap().errorProto)->newChild(ctx, /*isMutable=*/true);
    TransientPin pinExc(ctx, exc);
    const_cast<proto::ProtoObject*>(exc)->setAttribute(
        ctx, msgTextKey(ctx), ctx->fromUTF8String(ExecutionEngine::kNestingLimitMessage));
    const_cast<proto::ProtoObject*>(exc)->setAttribute(ctx, resumableKey(ctx), PROTO_FALSE);
    return exc;
}

// S23: the handler `signal` found cannot start, because the engine nesting
// limit is reached. Raise the limit's Error at this point instead: search for
// its handler from where the current search stands (`searchBelowId`, so a
// handler that already did `pass` is not entered again) and unwind to that
// handler's `on:do:`, which runs it there (NestingLimitUnwind.h). It cannot
// run here: it needs an engine as well. With no handler, the Error's default
// action ends the activation. Never returns normally.
const proto::ProtoObject* raiseNestingLimitError(STRuntime& rt, proto::ProtoContext* ctx,
                                                 proto::proto_ulong searchBelowId) {
    const proto::ProtoObject* limitError = newNestingLimitError(rt, ctx);
    TransientPin pinLimit(ctx, limitError);
    if (const HandlerEntry* entry = handlerStackFindMatch(ctx, limitError, searchBelowId))
        throw NestingLimitUnwind{ entry->handlerId };
    defaultAction(rt, ctx, limitError);   // an Error: throws UnhandledSTException
    throw std::logic_error("the default action of an Error returned");
}

const proto::ProtoObject* signalInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exc,
                                          proto::proto_ulong firstHandlerId,
                                          const std::exception_ptr* origin) {
    // Pin the instance for the whole primitive — it is held across handler-
    // stack walks and invokeBlock (which spins a full nested engine).
    TransientPin pinExc(ctx, exc);
    const bool foreign = !isExceptionInstance(rt, ctx, exc);

    proto::proto_ulong searchBelowId = 0;   // 0 == search from the top of the stack
    for (;;) {
        const HandlerEntry* entry = firstHandlerId != 0
            ? handlerStackFindById(firstHandlerId)
            : handlerStackFindMatch(ctx, exc, searchBelowId,
                                    // `on: Exception do:` catches every value
                                    foreign ? rt.bootstrap().exceptionProto : nullptr);
        firstHandlerId = 0;
        if (!entry) {
            // No (further) matching handler: run the exception's default
            // action. For a resumable exception this returns a value that
            // `signal` yields; for an Error it throws; a foreign exception
            // continues outward.
            if (foreign) foreignDefaultAction(rt, ctx, exc, origin);
            return defaultAction(rt, ctx, exc);
        }

        proto::proto_ulong handlerId          = entry->handlerId;
        const proto::ProtoObject* hBlock = entry->handlerBlock;

        // S23: no engine is left for the handler block at this depth.
        if (!ExecutionEngine::canStartNestedEngine())
            return raiseNestingLimitError(rt, ctx, searchBelowId);

        // Disable this entry and every inner one, so a `signal` raised while
        // the handler block runs is caught by an OUTER handler.
        std::vector<proto::proto_ulong> disabled = handlerStackDisableFrom(handlerId);

        const proto::ProtoObject* handlerResult = nullptr;
        // What leaves the handler block is decided inside the catch clauses
        // and acted on after them: an exception that is not consumed here is
        // re-thrown with std::rethrow_exception once the clause has ended, not
        // with `throw;` inside it -- see translateNativeException
        // (NativeExceptionBridge.h) for why.
        std::exception_ptr propagate;
        const proto::ProtoObject* resumed = nullptr;
        bool passed = false;
        try {
            // The handler actions (`return:`, `resume:`, `retry`, `pass`) —
            // which only receive the exception as their receiver — find this
            // entry through the active-signal stack while the handler runs.
            ActiveSignalScope active(exc, handlerId, foreign);
            const proto::ProtoObject* a0 = exc;
            // The handler is culled, as in Pharo: a block with no argument
            // (on: Error do: [nil]) is evaluated without the exception.
            handlerResult = blockArgCount(ctx, hBlock) == 0
                ? invokeBlock(rt, ctx, hBlock, nullptr, 0)
                : invokeBlock(rt, ctx, hBlock, &a0, 1);
        } catch (const ResumeSignal& r) {
            // `resume: v` -- only ours is consumed here; an inner id belongs
            // to an outer signal loop.
            if (r.handlerId() != handlerId) propagate = std::current_exception();
            else resumed = r.value() ? r.value() : PROTO_NONE;
        } catch (const PassSignal& p) {
            // `pass` / `outer` -- search outward for the next matching handler.
            if (p.handlerId() != handlerId) propagate = std::current_exception();
            else passed = true;
        } catch (...) {
            // UnwindToHandler (return:), RetrySignal (retry), NonLocalReturn,
            // a cooperative yield, or another error: let it continue.
            propagate = std::current_exception();
        }
        // Re-enable what we disabled, so the handler stack stays consistent
        // for outer handlers / the owning `on:do:`, whatever happens next.
        handlerStackRestore(disabled);
        if (propagate) std::rethrow_exception(propagate);
        // `signal` returns v: the protected block (its stack never unwound)
        // continues from the signal point.
        if (resumed) return resumed;
        if (passed) {
            searchBelowId = handlerId;   // resume search strictly outer to this
            continue;
        }

        // The handler fell off its end without return:/resume:/retry/pass.
        // Fall-through == `return: handlerResult`.
        throw UnwindToHandler(ctx, handlerId,
                              handlerResult ? handlerResult : PROTO_NONE);
    }
}

} // namespace

// anException __isResumable → whether resume: is allowed (the class-derived
// marker, overridable per instance). Exception>>isResumable wraps it.
const proto::ProtoObject* prim_Exception_isResumable(STRuntime&, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const*, int) {
    return isResumable(ctx, r) ? PROTO_TRUE : PROTO_FALSE;
}

// Re-signal an exception instance in the current context: a Future rejected
// by an actor's unhandled exception raises that same exception in the waiter.
const proto::ProtoObject* resignalException(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* exc) {
    return signalInstance(rt, ctx, exc);
}

bool isExceptionClassObject(STRuntime& rt, proto::ProtoContext* ctx,
                            const proto::ProtoObject* obj) {
    return obj && obj != PROTO_NONE && !obj->isInteger(ctx) && !obj->asString(ctx)
        && (obj == rt.bootstrap().errorProto
            || obj->hasParent(ctx, rt.bootstrap().errorProto) != 0);
}

bool isExceptionInstance(STRuntime& rt, proto::ProtoContext* ctx,
                         const proto::ProtoObject* obj) {
    return obj && obj != PROTO_NONE && !obj->isInteger(ctx) && !obj->asString(ctx)
        && obj->hasParent(ctx, rt.bootstrap().exceptionProto) != 0;
}

// --- EXC-d: translate a native C++ exception into a protoST Error ----------
//
// Declared in NativeExceptionBridge.h; called by `translateNativeException`'s
// catch branches at every native call boundary.
//
// Builds a fresh, NON-resumable `Error` instance — a mutable child of the
// `Error` prototype, so it inherits `__resumable__ == false` and is caught by
// an ordinary `on: Error do:` guard — stamps `messageText`, and runs it
// through the very same `signalInstance` path a script-level `Error signal:`
// uses. An active handler therefore catches it; with no handler the search
// exhausts and `defaultAction` throws `UnhandledSTException`.
//
// May throw (all correct, all propagate out of the wrapper): `UnwindToHandler`
// when a handler caught the translated Error and did `return:` / fell through,
// `RetrySignal` on `retry`, `UnhandledSTException` when nothing caught it.
// MNT-b2 (D3 / D8): signal a fresh, NON-resumable instance of `errorClass`
// (which MUST be `Error` or a subclass of it) carrying `message` as its
// `messageText`, through the very same `signalInstance` path a script-level
// `Error signal:` uses. An active handler — guarding the specific class OR the
// `Error` base — catches it; with no handler `defaultAction` throws
// `UnhandledSTException`. This is the shared core of `signalNativeError`
// (errorClass == Error) and of the runtime-internal `doesNotUnderstand` /
// dead-home-return signals (errorClass == MessageNotUnderstood /
// BlockCannotReturn).
const proto::ProtoObject* signalErrorOfClass(STRuntime& rt,
                                             proto::ProtoContext* ctx,
                                             const proto::ProtoObject* errorClass,
                                             const char* message) {
    // Fresh mutable child of the error class — inherits the non-resumable
    // marker from Error and a chain to objectProto.
    const proto::ProtoObject* exc =
        const_cast<proto::ProtoObject*>(errorClass)->newChild(ctx, /*isMutable=*/true);
    TransientPin pinExc(ctx, exc);
    const proto::ProtoObject* text =
        ctx->fromUTF8String(message ? message : "error");
    const_cast<proto::ProtoObject*>(exc)->setAttribute(
        ctx, msgTextKey(ctx), text);
    // Force non-resumable on the instance too, defensively — a runtime-raised
    // error of this kind is never resumable (the offending stack is gone).
    const_cast<proto::ProtoObject*>(exc)->setAttribute(
        ctx, resumableKey(ctx), PROTO_FALSE);
    return signalInstance(rt, ctx, exc);
}

// Signal a fresh instance of the global class named `className` (an Error
// subclass defined by the kernel, such as SubscriptOutOfBounds), or a plain
// Error when no such global exists.
const proto::ProtoObject* signalErrorNamed(STRuntime& rt, proto::ProtoContext* ctx,
                                           const char* className, const char* message) {
    const proto::ProtoObject* g = rt.globals();
    const proto::ProtoObject* cls = g
        ? g->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, className)) : nullptr;
    if (!cls || cls == PROTO_NONE || !isExceptionClassObject(rt, ctx, cls))
        return signalNativeError(rt, ctx, message);
    return signalErrorOfClass(rt, ctx, cls, message);
}

// A Message: the selector and the arguments of a send, as a
// doesNotUnderstand: override and MessageNotUnderstood>>message see it.
const proto::ProtoObject* makeMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                      const std::string& selector,
                                      const proto::ProtoObject* const* args, int argc) {
    const proto::ProtoList* list = ctx->newList();
    TransientPin pinList(ctx, reinterpret_cast<const proto::ProtoObject*>(list));
    for (int i = 0; i < argc; ++i) {
        list = list->appendLast(ctx, args[i] ? args[i] : PROTO_NONE);
        pinList.reset(reinterpret_cast<const proto::ProtoObject*>(list));
    }
    const proto::ProtoObject* arr = rt.bootstrap().arrayProto->newChild(ctx, /*isMutable=*/true);
    TransientPin pinArr(ctx, arr);
    arr->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"), list->asObject(ctx));
    const proto::ProtoObject* msg = rt.bootstrap().messageProto->newChild(ctx, /*isMutable=*/true);
    TransientPin pinMsg(ctx, msg);
    msg->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__selector__"),
                      reinterpret_cast<const proto::ProtoObject*>(
                          proto::ProtoString::createSymbol(ctx, selector.c_str())));
    msg->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__arguments__"), arr);
    return msg;
}

// Signal a MessageNotUnderstood carrying the receiver and the Message. It is
// resumable, as in Pharo: resume: answers the value for the failed send.
const proto::ProtoObject* signalMessageNotUnderstood(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* receiver,
                                                     const proto::ProtoObject* message,
                                                     const char* text) {
    const proto::ProtoObject* exc =
        const_cast<proto::ProtoObject*>(rt.bootstrap().messageNotUnderstoodProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinExc(ctx, exc);
    exc->setAttribute(ctx, msgTextKey(ctx), ctx->fromUTF8String(text ? text : "doesNotUnderstand:"));
    exc->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__receiver__"),
                      receiver ? receiver : PROTO_NONE);
    exc->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__message__"),
                      message ? message : PROTO_NONE);
    return signalInstance(rt, ctx, exc);
}

const proto::ProtoObject* signalNativeError(STRuntime& rt,
                                            proto::ProtoContext* ctx,
                                            const char* message) {
    return signalErrorOfClass(rt, ctx, rt.bootstrap().errorProto,
                              message ? message : "native exception");
}

// --- protoCore 3.0.0: a ProtoException at a native boundary ----------------
//
// Declared in NativeExceptionBridge.h; called by translateNativeException
// after its try statement, with the caught exception kept alive in `caught`
// (which keeps `value` pinned). The value is signalled unchanged, so the
// handler receives the very object that was raised:
//   * a protoST exception instance -- protoCore's own error, which the error
//     factory built as a ZeroDivide / InvalidArgument / ..., or a protoST
//     exception that crossed another runtime -- is signalled like `signal`:
//     it keeps its class's resumability and, unhandled, goes to its default
//     action;
//   * any other value is a foreign exception: matched by its prototype chain,
//     non-resumable, and re-thrown as `caught` itself when no handler takes it.
const proto::ProtoObject* signalProtoException(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* value,
                                               std::exception_ptr caught) {
    if (isExceptionInstance(rt, ctx, value)) return signalInstance(rt, ctx, value);
    return signalInstance(rt, ctx, value, 0, &caught);
}

// The Exception protocol for a foreign exception while its handler runs: the
// selector is looked up on Error, and the method runs with the foreign value
// as its receiver (messageText, description, return:, retry, pass, outer,
// signal, isResumable, ...). Answers false, without running anything, when
// `recv` is not a foreign exception being handled on this thread or Error has
// no such method -- the send is then not understood, as before.
bool canSendToForeignException(STRuntime& rt, proto::ProtoContext* ctx,
                               const proto::ProtoObject* recv,
                               const proto::ProtoString* selector) {
    const ActiveSignal* active = recv ? findActiveSignal(recv) : nullptr;
    if (!active || !active->foreign) return false;
    const proto::ProtoObject* m = rt.bootstrap().errorProto->getAttribute(ctx, selector);
    return m && m != PROTO_NONE;
}

const proto::ProtoObject* sendToForeignException(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* recv,
                                                 const proto::ProtoString* selector,
                                                 const proto::ProtoObject* const* args, int argc) {
    bool understood = false;
    const proto::ProtoObject* r = sendDynamicFrom(rt, ctx, recv, rt.bootstrap().errorProto,
                                                  selector, args, argc, &understood);
    if (!understood)
        throw std::runtime_error("doesNotUnderstand: " + selector->toStdString(ctx));
    return r ? r : PROTO_NONE;
}

// --- Describing an uncaught exception -------------------------------------

std::string describeUncaught(const std::exception& e) {
    return std::string("error: ") + e.what();
}

std::string describeProtoException(proto::ProtoContext* ctx, const proto::ProtoException& e) {
    if (const auto* u = dynamic_cast<const UnhandledSTException*>(&e)) return u->message();
    const proto::ProtoObject* v = e.value();
    // A protoST exception that did not go through this runtime's default
    // action (raised by another runtime with a protoST value).
    const std::string text = messageTextOf(ctx, v);
    std::string className = stringAttribute(ctx, v, "__class_name__");
    if (!text.empty() || !className.empty()) {
        if (className.empty() || className == "Error" || text.empty()) return text.empty() ? className : text;
        return className + ": " + text;
    }
    return describeForeignValue(ctx, v);
}

std::string describeUncaught(proto::ProtoContext* ctx, const proto::ProtoException& e) {
    std::string out = "error: " + describeProtoException(ctx, e);
    if (const auto* u = dynamic_cast<const UnhandledSTException*>(&e))
        if (!u->trace().empty()) out += "\n" + u->trace();
    return out;
}

// The description of a value used as an exception -- a protoST exception, a
// module's error, protoCore's own error value -- for messages such as a
// rejected Future's.
std::string describeExceptionValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const std::string text = messageTextOf(ctx, v);
    if (!text.empty()) return text;
    return describeForeignValue(ctx, v);
}

// --- protoCore's errors as protoST exceptions (the error factory) ---------
//
// protoCore raises its own failures -- a division by zero, a malformed
// integer literal, an integer read out of range, an operand of the wrong
// type, a call with too many arguments -- as ProtoExceptions whose value the
// space's error factory builds. This factory builds them as instances of
// protoST's own classes, the way `aClass new messageText: text` would, so
// `on: ZeroDivide do:` and `on: Error do:` catch them wherever they happen,
// in a primitive or inside a module:
//
//   ProtoErrorKind     protoST class       resumable
//   DivisionByZero     ZeroDivide          yes (ArithmeticError, as in Pharo)
//   Overflow           ArithmeticError     yes
//   InvalidArgument    InvalidArgument     no  (kernel exception.st)
//   TypeMismatch       InvalidArgument     no
//   ArityMismatch      Error               no
//
// The classes are reached through the runtime registered for the space
// (stRuntimeForSpace): the bootstrap prototypes and the kernel globals, all
// pinned for the runtime's lifetime. Before the kernel defines
// InvalidArgument it is a plain Error; with no runtime registered (a space
// being torn down) the value is protoCore's default one. The factory never
// throws: on any failure it answers protoCore's default value, or nil.
namespace {
const proto::ProtoObject* protoCoreDefaultError(proto::ProtoContext* ctx, proto::ProtoErrorKind kind,
                                                const char* message) {
    proto::ProtoContext::CriticalSection cs(ctx);
    const proto::ProtoObject* text = ctx->fromUTF8String(message ? message : "error");
    return ctx->space->getErrorPrototype(ctx, kind)->newChild(ctx)->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "message"), text);
}

const proto::ProtoObject* stErrorFactory(proto::ProtoContext* ctx, proto::ProtoErrorKind kind,
                                         const char* message) {
    try {
        STRuntime* rt = ctx && ctx->space ? stRuntimeForSpace(ctx->space) : nullptr;
        if (!rt) return protoCoreDefaultError(ctx, kind, message);
        const Bootstrap& b = rt->bootstrap();
        const proto::ProtoObject* cls = b.errorProto;
        switch (kind) {
            case proto::ProtoErrorKind::DivisionByZero: cls = b.zeroDivideProto; break;
            case proto::ProtoErrorKind::Overflow:       cls = b.arithmeticErrorProto; break;
            case proto::ProtoErrorKind::InvalidArgument:
            case proto::ProtoErrorKind::TypeMismatch: {
                const proto::ProtoObject* g = rt->globals();
                const proto::ProtoObject* named = g
                    ? g->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "InvalidArgument"))
                    : nullptr;
                if (named && named != PROTO_NONE && isExceptionClassObject(*rt, ctx, named))
                    cls = named;
                break;
            }
            case proto::ProtoErrorKind::ArityMismatch:  cls = b.errorProto; break;
        }
        // No collection between the allocations: the text and the instance
        // are held only in C++ locals until protoCore pins the value.
        proto::ProtoContext::CriticalSection cs(ctx);
        const proto::ProtoObject* text = ctx->fromUTF8String(message ? message : "error");
        const proto::ProtoObject* exc = cls->newChild(ctx, /*isMutable=*/true);
        exc->setAttribute(ctx, msgTextKey(ctx), text);
        return exc;
    } catch (...) {
        try { return protoCoreDefaultError(ctx, kind, message); } catch (...) { return PROTO_NONE; }
    }
}
} // namespace

void installErrorFactory(STRuntime& rt) {
    rt.space()->setErrorFactory(&stErrorFactory);
}

namespace {

// --- Build an instance from a class object ---------------------------------
//
// Class-side `signal` / `signal:` desugar to `self new` then signal. `new` is
// `newChild` (BL-1), so a fresh mutable child of the class prototype.
const proto::ProtoObject* newExceptionInstance(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* cls) {
    return cls->newChild(ctx, /*isMutable=*/true);
}

// receiver signal
//
// Class-side  → build an instance, signal it.
// Instance-side → signal the receiver directly.
const proto::ProtoObject* prim_Exception_signal(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const*, int) {
    const proto::ProtoObject* exc = r;
    if (isClassObject(ctx, r)) {
        exc = newExceptionInstance(ctx, r);
    }
    return signalInstance(rt, ctx, exc);
}

// receiver signal: aString
//
// Class-side  → build an instance, set messageText, signal it.
// Instance-side → set messageText on the receiver, signal it.
const proto::ProtoObject* prim_Exception_signalText(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const* a,
                                                     int argc) {
    if (argc != 1) throw std::runtime_error("signal: expects 1 arg (message string)");
    const proto::ProtoObject* exc = r;
    if (isClassObject(ctx, r)) {
        exc = newExceptionInstance(ctx, r);
    }
    TransientPin pinExc(ctx, exc);
    const_cast<proto::ProtoObject*>(exc)->setAttribute(ctx, msgTextKey(ctx), a[0]);
    return signalInstance(rt, ctx, exc);
}

// anException messageText → the stored string, or the class name when unset
// The text lives under its own key: stored under `messageText`, an exception
// with no text found the messageText *method* marker on its prototype and
// answered it as an integer. With no text, answer the class name, as Pharo's
// `description` does.
const proto::ProtoObject* prim_Exception_messageText(STRuntime& rt, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* r,
                                                      const proto::ProtoObject* const*, int) {
    const proto::ProtoObject* m = r ? r->getAttribute(ctx, msgTextKey(ctx)) : nullptr;
    if (m && m != PROTO_NONE) return m;
    // A foreign exception (a module's error, protoCore's own error value)
    // carries its text in a `message` attribute.
    if (!isExceptionInstance(rt, ctx, r)) {
        const proto::ProtoObject* fm = (r && r != PROTO_NONE && r->isCell(ctx) && !r->asString(ctx))
            ? r->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "message")) : nullptr;
        if (fm && fm != PROTO_NONE && fm->isString(ctx)) return fm;
    }
    const proto::ProtoObject* cn = r ? r->getAttribute(ctx, classNameKey(ctx)) : nullptr;
    return (cn && cn != PROTO_NONE) ? cn : PROTO_NONE;
}

// anException messageText: aString → store it, return the receiver
const proto::ProtoObject* prim_Exception_setMessageText(STRuntime&, proto::ProtoContext* ctx,
                                                         const proto::ProtoObject* r,
                                                         const proto::ProtoObject* const* a,
                                                         int argc) {
    if (argc != 1) throw std::runtime_error("messageText: expects 1 arg");
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, msgTextKey(ctx), a[0]);
    return r;
}

// anException return: v
//
// Handler action: unwind to the matching `on:do:` and make it yield `v`. The
// target handler id is the one `signal` recorded for this instance on the
// active-signal stack.
const proto::ProtoObject* prim_Exception_return(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 1) throw std::runtime_error("return: expects 1 arg (value)");
    const proto::proto_ulong handlerId = activeHandlerIdOf(ctx, r);
    if (handlerId == 0) {
        // `return:` on an exception that is not currently being handled.
        throw std::runtime_error("return: sent to an exception with no active handler");
    }
    throw UnwindToHandler(ctx, handlerId, a[0] ? a[0] : PROTO_NONE);
}

// anException resume: v  /  anException resume
//
// Handler action: make the matching `signal` RETURN `v` without unwinding —
// the protected computation continues from the signal point. Valid only for a
// resumable exception; `resume:` on an `Error` is itself an error.
const proto::ProtoObject* prim_Exception_resume(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc > 1)
        throw std::runtime_error("resume: expects 0 or 1 arg (value)");
    if (!isResumable(ctx, r)) {
        // `resume:` on a non-resumable exception (an Error). The native stack
        // analogue is already gone — reject it as a hard error.
        throw std::runtime_error("cannot resume a non-resumable exception");
    }
    proto::proto_ulong handlerId = activeHandlerIdOf(ctx, r);
    if (handlerId == 0)
        throw std::runtime_error("resume: sent to an exception with no active handler");
    // `resume` with no argument == `resume: nil`.
    const proto::ProtoObject* v = (argc == 1 && a[0]) ? a[0] : PROTO_NONE;
    throw ResumeSignal(ctx, handlerId, v);
}

// anException retry
//
// Handler action: unwind to the owning `on:do:` and re-evaluate the protected
// block from scratch. Propagates PAST `signal` to the `on:do:` primitive.
const proto::ProtoObject* prim_Exception_retry(STRuntime&, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const*,
                                                int) {
    proto::proto_ulong handlerId = activeHandlerIdOf(ctx, r);
    if (handlerId == 0)
        throw std::runtime_error("retry sent to an exception with no active handler");
    throw RetrySignal{ handlerId };
}

// anException pass  (a.k.a. anException outer)
//
// Handler action: resume the handler search OUTWARD from the current handler.
// Caught by `signal`'s loop. For this MVP `outer` is an alias of `pass` — see
// PassSignal.h for the documented simplification.
const proto::ProtoObject* prim_Exception_pass(STRuntime&, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*,
                                               int) {
    proto::proto_ulong handlerId = activeHandlerIdOf(ctx, r);
    if (handlerId == 0)
        throw std::runtime_error("pass sent to an exception with no active handler");
    throw PassSignal{ handlerId };
}

// S23: run the handler with id `handlerId` (pushed by the calling `on:do:`)
// on a fresh nesting-limit Error, after a NestingLimitUnwind brought the
// unwind to that `on:do:`. The outcome is dispatched by signalInstance as for
// any signal; the Error is not resumable, so this returns only if a handler
// misbehaves.
const proto::ProtoObject* signalNestingLimitAt(STRuntime& rt, proto::ProtoContext* ctx,
                                               proto::proto_ulong handlerId) {
    const proto::ProtoObject* limitError = newNestingLimitError(rt, ctx);
    return signalInstance(rt, ctx, limitError, handlerId);
}

// --- Shared protected-block runner -----------------------------------------
//
// Backs both `on:do:` and `on:do:on:do:`. Pushes one HandlerEntry per
// (guard, handler) pair, runs the protected block, and pops every entry on
// EVERY exit path. The pairs arrive innermost-LAST (matching single `on:do:`
// where the sole guard is the innermost); they are pushed in order so the
// LAST pair is the innermost / first-searched entry — but since the search
// order among sibling guards on one block is by stack recency, the caller
// passes them so the intended priority holds. An UnwindToHandler /
// RetrySignal carrying ANY of this construct's ids is for this construct;
// retry re-evaluates the protected block with a fresh set of handler ids.
// 2026-05-24 perf: single-guard fast path. The original (multi-guard)
// runProtected lives below as `runProtectedMulti`; the
// single-guard case (`[…] on: C do: H` — by far the dominant shape)
// now skips three std::vector allocations per call and reads a single
// id off the stack instead of via vector index. `exception_latency`
// fires this path 50 K times; perf-traced 1.65 % in `_int_malloc`
// before this change, almost entirely from the ids vector allocation
// per attempt.
const proto::ProtoObject* runProtectedSingle(
        STRuntime& rt, proto::ProtoContext* ctx,
        const proto::ProtoObject* protectedBlock,
        const proto::ProtoObject* guardClass,
        const proto::ProtoObject* handlerBlock) {
    // An exception that is not ours is re-thrown after the catch clause
    // (std::rethrow_exception), not with `throw;` inside it: see
    // translateNativeException (NativeExceptionBridge.h) for why.
    std::exception_ptr notOurs;
    proto::proto_ulong id = 0;
    // S23: set when a NestingLimitUnwind for this construct arrived. The turn
    // then keeps the entry pushed and runs the handler on the nesting limit's
    // Error instead of the protected block (see NestingLimitUnwind.h).
    bool limitPending = false;
    for (;;) {   // each turn is one attempt; `retry` loops back here
        if (!limitPending) id = handlerStackPush(guardClass, handlerBlock);

        try {
            const proto::ProtoObject* result = limitPending
                ? signalNestingLimitAt(rt, ctx, id)
                : invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
            handlerStackPop(id);
            return result;
        } catch (const UnwindToHandler& u) {
            handlerStackPop(id);
            if (u.handlerId() == id)
                return u.value() ? u.value() : PROTO_NONE;
            notOurs = std::current_exception();
        } catch (const RetrySignal& r) {
            handlerStackPop(id);
            if (r.handlerId() == id) { limitPending = false; continue; }
            notOurs = std::current_exception();
        } catch (const NestingLimitUnwind& n) {
            if (n.handlerId() == id && !limitPending) { limitPending = true; continue; }
            handlerStackPop(id);
            notOurs = std::current_exception();
        } catch (...) {
            handlerStackPop(id);
            notOurs = std::current_exception();
        }
        std::rethrow_exception(notOurs);
    }
}

const proto::ProtoObject* runProtected(
        STRuntime& rt, proto::ProtoContext* ctx,
        const proto::ProtoObject* protectedBlock,
        const std::vector<std::pair<const proto::ProtoObject*,
                                    const proto::ProtoObject*>>& guards) {
    // Single-guard shortcut — avoids the std::vector<unsigned long>
    // per-attempt allocation. Multi-guard (`on:do:on:do:`) falls through
    // to the legacy loop below.
    if (guards.size() == 1) {
        return runProtectedSingle(rt, ctx, protectedBlock,
                                  guards[0].first, guards[0].second);
    }
    std::vector<proto::proto_ulong> ids;
    proto::proto_ulong limitTarget = 0;   // S23: as limitPending in runProtectedSingle
    for (;;) {   // each turn is one attempt; `retry` loops back here
        if (limitTarget == 0) {
            ids.clear();
            ids.reserve(guards.size());
            for (const auto& g : guards)
                ids.push_back(handlerStackPush(g.first, g.second));
        }

        auto popAll = [&]() {
            // Idempotent; pop newest-first so the stack stays well-formed.
            for (std::size_t i = ids.size(); i-- > 0; )
                handlerStackPop(ids[i]);
        };
        auto owns = [&](proto::proto_ulong id) {
            for (proto::proto_ulong x : ids) if (x == id) return true;
            return false;
        };

        // Not ours: re-thrown after the catch clause, as in
        // runProtectedSingle.
        std::exception_ptr notOurs;
        try {
            const proto::ProtoObject* result = limitTarget != 0
                ? signalNestingLimitAt(rt, ctx, limitTarget)
                : invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
            popAll();
            return result;
        } catch (const UnwindToHandler& u) {
            popAll();
            if (owns(u.handlerId()))
                return u.value() ? u.value() : PROTO_NONE;
            notOurs = std::current_exception();   // targets an OUTER construct
        } catch (const RetrySignal& r) {
            popAll();
            if (owns(r.handlerId())) {
                limitTarget = 0;
                continue;   // re-evaluate the protected block — loop again
            }
            notOurs = std::current_exception();   // targets an OUTER construct
        } catch (const NestingLimitUnwind& n) {
            if (limitTarget == 0 && owns(n.handlerId())) {
                limitTarget = n.handlerId();   // entries stay pushed
                continue;
            }
            popAll();
            notOurs = std::current_exception();
        } catch (...) {
            // NonLocalReturn, FutureYield, ResumeSignal/PassSignal escaping a
            // bug, std::exception — all must leave the handler stack balanced.
            popAll();
            notOurs = std::current_exception();
        }
        std::rethrow_exception(notOurs);
    }
}

// protectedBlock on: GuardClass do: handlerBlock
const proto::ProtoObject* prim_Block_on_do(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* protectedBlock,
                                            const proto::ProtoObject* const* a,
                                            int argc) {
    if (argc != 2)
        throw std::runtime_error("on:do: expects 2 args (guard class, handler block)");
    // 2026-05-24 perf: skip the std::vector<std::pair<...>> build that
    // runProtected (multi-guard variant) requires — call the
    // single-guard fast path directly. `exception_latency` fires this
    // primitive 50 K times; the guards-vector heap allocation was
    // visible at ~1 % of CPU in perf.
    return runProtectedSingle(rt, ctx, protectedBlock, a[0], a[1]);
}

// protectedBlock on: G1 do: H1 on: G2 do: H2
//
// Two guards on one protected block. Both entries are pushed (G1 first, then
// G2 — so G2 is the innermost / searched first when both would match); the
// protected block runs once; an unwind / retry carrying either id is honoured.
const proto::ProtoObject* prim_Block_on_do_on_do(STRuntime& rt,
                                                  proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* protectedBlock,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 4)
        throw std::runtime_error(
            "on:do:on:do: expects 4 args (guard, handler, guard, handler)");
    std::vector<std::pair<const proto::ProtoObject*, const proto::ProtoObject*>> guards;
    guards.emplace_back(a[0], a[1]);
    guards.emplace_back(a[2], a[3]);
    return runProtected(rt, ctx, protectedBlock, guards);
}

// --- EXC-c: ensure: / ifCurtailed: -----------------------------------------
//
// `[ block ] ensure: [ cleanup ]` runs `cleanup` whether `block` exits
// normally OR is abandoned by an unwind. `[ block ] ifCurtailed: [ cleanup ]`
// runs `cleanup` ONLY on an abnormal (unwound) exit.
//
// The spec (§5) describes registering the cleanup against the frame so the
// `frames_` unwinder fires it. For the current engine — where the protected
// block runs via `invokeBlock`, a full nested ExecutionEngine — the concrete
// realisation is simpler and exact: a C++ try/catch around `invokeBlock`. Any
// unwinding exception that abandons the protected block (`UnwindToHandler`
// from a handler's `return:`/fall-through, `RetrySignal` from `retry`,
// `NonLocalReturn` from `^expr`, or any `std::exception`) propagates out of
// `invokeBlock` as a C++ throw and is seen by `catch (...)`.
//
// FutureYield is deliberately EXCLUDED from triggering the cleanup: a
// cooperative yield is a SUSPENSION of the protected block, not a termination
// of it. The block will be resumed later from the snapshot, at which point it
// finishes (or unwinds) normally. Running the cleanup on the yield would (a)
// be semantically wrong — the protected computation has not finished — and
// (b) run the cleanup a SECOND time when the block later completes. So
// FutureYield is caught specifically before `catch (...)` and merely
// re-thrown.
//
// ResumeSignal / PassSignal are not a concern here: they are thrown by a
// handler running INSIDE `signal`, which runs INSIDE the protected block;
// `signal`'s own loop consumes them before control ever leaves the protected
// block, so they never reach this primitive.
//
// Re-throw correctness: the `catch (...)` branch records the exception; the
// cleanup runs after the catch clause and std::rethrow_exception re-throws the
// ORIGINAL exception, so an
// UnwindToHandler / RetrySignal / NonLocalReturn still reaches its real
// target frame.
//
// Sharp edge (documented, not engineered around): if the cleanup block itself
// unwinds while an exception is already in flight, ordinary C++ semantics
// apply (a throw escaping a catch during an active exception calls
// std::terminate). The common, supported path is a normally-completing
// cleanup.
//
// Block-capture limitation: block closures currently run with
// `self == PROTO_NONE` and do not capture method locals / instance variables
// (a deferred F3 gap, see block_prims.cpp). A cleanup block therefore cannot
// read the enclosing method's `self` or locals; it observes its effect
// through state reachable by a block (e.g. an attribute on a globally-named
// object) — the same pattern EXC-b's `retry` test uses.

// protectedBlock ensure: cleanupBlock
const proto::ProtoObject* prim_Block_ensure(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* protectedBlock,
                                            const proto::ProtoObject* const* a,
                                            int argc) {
    if (argc != 1)
        throw std::runtime_error("ensure: expects 1 arg (cleanup block)");
    const proto::ProtoObject* cleanupBlock = a[0];
    // The cleanup and the re-throw run after the catch clause, once the
    // frames below this primitive are released: see translateNativeException
    // (NativeExceptionBridge.h) for why.
    std::exception_ptr exit;
    bool cleanup = false;
    try {
        const proto::ProtoObject* result =
            invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
        // Normal exit → run the cleanup; its value is discarded.
        invokeBlock(rt, ctx, cleanupBlock, nullptr, 0);
        return result;
    } catch (const FutureYield&) {
        // A yield is a suspension, not an exit — propagate without cleanup.
        exit = std::current_exception();
    } catch (...) {
        // Abnormal exit (UnwindToHandler / RetrySignal / NonLocalReturn /
        // std::exception) → run the cleanup, then re-propagate the original.
        exit = std::current_exception();
        cleanup = true;
    }
    if (cleanup) invokeBlock(rt, ctx, cleanupBlock, nullptr, 0);
    std::rethrow_exception(exit);
}

// protectedBlock ifCurtailed: cleanupBlock
//
// Identical to `ensure:` but the cleanup runs ONLY on an abnormal exit — the
// normal path returns the result WITHOUT running the cleanup.
const proto::ProtoObject* prim_Block_ifCurtailed(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* protectedBlock,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 1)
        throw std::runtime_error("ifCurtailed: expects 1 arg (cleanup block)");
    const proto::ProtoObject* cleanupBlock = a[0];
    // After the catch clause, as in ensure:.
    std::exception_ptr exit;
    bool cleanup = false;
    try {
        const proto::ProtoObject* result =
            invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
        // Normal exit → cleanup SKIPPED.
        return result;
    } catch (const FutureYield&) {
        // A yield is a suspension, not a curtailment — propagate, no cleanup.
        exit = std::current_exception();
    } catch (...) {
        // Abnormal exit only → run the cleanup, then re-propagate.
        exit = std::current_exception();
        cleanup = true;
    }
    if (cleanup) invokeBlock(rt, ctx, cleanupBlock, nullptr, 0);
    std::rethrow_exception(exit);
}

// Accessors of MessageNotUnderstood and Message.
const proto::ProtoObject* attrOrNil(proto::ProtoContext* ctx, const proto::ProtoObject* r,
                                    const char* key) {
    const proto::ProtoObject* v = r ? r->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, key)) : nullptr;
    return (v && v != PROTO_NONE) ? v : PROTO_NONE;
}
const proto::ProtoObject* prim_MNU_receiver(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r, const proto::ProtoObject* const*, int) {
    return attrOrNil(ctx, r, "__receiver__");
}
const proto::ProtoObject* prim_MNU_message(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r, const proto::ProtoObject* const*, int) {
    return attrOrNil(ctx, r, "__message__");
}
const proto::ProtoObject* prim_Message_selector(STRuntime&, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r, const proto::ProtoObject* const*, int) {
    return attrOrNil(ctx, r, "__selector__");
}
const proto::ProtoObject* prim_Message_arguments(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r, const proto::ProtoObject* const*, int) {
    return attrOrNil(ctx, r, "__arguments__");
}

} // namespace

void installExceptionPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();

    // protoCore's own errors arrive as protoST exceptions (stErrorFactory).
    installErrorFactory(rt);

    // signal / signal: — bound on Exception, inherited by Error / Warning and
    // by user subclasses. Works both class-side (`Error signal: 'x'`) and
    // instance-side (`anError signal`).
    int signalIdx     = reg.registerPrim(prim_Exception_signal);
    int signalTextIdx = reg.registerPrim(prim_Exception_signalText);
    bindPrimitive(rt, b.exceptionProto, "signal",  signalIdx);
    bindPrimitive(rt, b.exceptionProto, "signal:", signalTextIdx);

    // Accessors + the handler actions.
bindPrimitive(rt, b.messageNotUnderstoodProto, "receiver", reg.registerPrim(prim_MNU_receiver));
    bindPrimitive(rt, b.messageNotUnderstoodProto, "message", reg.registerPrim(prim_MNU_message));
    bindPrimitive(rt, b.messageProto, "selector", reg.registerPrim(prim_Message_selector));
    bindPrimitive(rt, b.messageProto, "arguments", reg.registerPrim(prim_Message_arguments));
        bindPrimitive(rt, b.exceptionProto, "messageText",
                  reg.registerPrim(prim_Exception_messageText));
    bindPrimitive(rt, b.exceptionProto, "messageText:",
                  reg.registerPrim(prim_Exception_setMessageText));
    bindPrimitive(rt, b.exceptionProto, "return:",
                  reg.registerPrim(prim_Exception_return));

    bindPrimitive(rt, b.exceptionProto, "__isResumable",
                  reg.registerPrim(prim_Exception_isResumable));

    // EXC-b handler actions: resume:/resume, retry, pass/outer.
    int resumeIdx = reg.registerPrim(prim_Exception_resume);
    bindPrimitive(rt, b.exceptionProto, "resume:", resumeIdx);
    bindPrimitive(rt, b.exceptionProto, "resume",  resumeIdx);
    bindPrimitive(rt, b.exceptionProto, "retry",
                  reg.registerPrim(prim_Exception_retry));
    int passIdx = reg.registerPrim(prim_Exception_pass);
    bindPrimitive(rt, b.exceptionProto, "pass",  passIdx);
    bindPrimitive(rt, b.exceptionProto, "outer", passIdx);   // MVP alias

    // on:do: / on:do:on:do: — the protected-block primitives, on blockProto.
    bindPrimitive(rt, b.blockProto, "on:do:",
                  reg.registerPrim(prim_Block_on_do));
    bindPrimitive(rt, b.blockProto, "on:do:on:do:",
                  reg.registerPrim(prim_Block_on_do_on_do));

    // EXC-c: ensure: / ifCurtailed: — the cleanup primitives, on blockProto.
    bindPrimitive(rt, b.blockProto, "ensure:",
                  reg.registerPrim(prim_Block_ensure));
    bindPrimitive(rt, b.blockProto, "ifCurtailed:",
                  reg.registerPrim(prim_Block_ifCurtailed));
}

} // namespace protoST
