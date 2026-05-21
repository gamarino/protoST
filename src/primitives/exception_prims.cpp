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
//     finds the newest enabled matching entry, stamps the handler id onto the
//     instance, disables that entry plus every inner one, and runs the handler
//     block IN PLACE. The handler outcome is then dispatched:
//       - fall-through / `return:` → throw UnwindToHandler (unwind the
//         protected block, the `on:do:` yields the value);
//       - `resume:` (ResumeSignal) → caught here; `signal` RETURNS the value,
//         the protected block continues from the signal point;
//       - `pass` / `outer` (PassSignal) → caught here; the loop searches
//         OUTWARD for the next matching handler;
//       - `retry` (RetrySignal) → NOT caught here; propagates to `on:do:`.
//     With NO matching handler the exception's default action runs (EXC-b
//     refinement: a resumable Exception/Warning resumes with nil; an Error /
//     non-resumable one aborts via std::runtime_error).

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/HandlerStack.h"
#include "runtime/UnwindToHandler.h"
#include "runtime/ResumeSignal.h"
#include "runtime/RetrySignal.h"
#include "runtime/PassSignal.h"
#include "runtime/FutureYield.h"
#include "runtime/UnhandledSTException.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

// invokeBlock — defined in block_prims.cpp. Runs a BlockClosure synchronously
// in a fresh nested ExecutionEngine. An UnwindToHandler thrown inside the
// block (or inside `signal` which the block called) is NOT a std::exception,
// so it bubbles straight through invokeBlock's nested engine untouched.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc);

namespace {

// Attribute keys. Resolved fresh from the live ctx each call — protoCore
// interns symbols per-ProtoSpace, so a function-local static would bind to
// the first runtime's space and dangle for every later STRuntime.
const proto::ProtoString* msgTextKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "messageText");
}
const proto::ProtoString* activeHandlerKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__active_handler_id__");
}
const proto::ProtoString* classNameKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__class_name__");
}
const proto::ProtoString* resumableKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__resumable__");
}

// True when the exception instance is resumable. The `__resumable__` marker is
// installed class-side at bootstrap (`Exception`/`Warning` true, `Error`
// false) and inherited by instances and user subclasses; an instance may
// override it. Absent marker is treated as resumable (the `Exception` root
// default) — only `Error` and its descendants stamp `false`.
bool isResumable(proto::ProtoContext* ctx, const proto::ProtoObject* exc) {
    if (!exc) return false;
    const proto::ProtoObject* m = exc->getAttribute(ctx, resumableKey(ctx));
    if (!m || m == PROTO_NONE) return true;
    return m == PROTO_TRUE;
}

// The active handler id stamped onto `exc` by `signal`, or 0 when the
// exception is not currently being handled.
unsigned long activeHandlerIdOf(proto::ProtoContext* ctx,
                                const proto::ProtoObject* exc) {
    const proto::ProtoObject* idObj =
        exc ? exc->getAttribute(ctx, activeHandlerKey(ctx)) : nullptr;
    if (!idObj || idObj == PROTO_NONE) return 0;
    return static_cast<unsigned long>(idObj->asLong(ctx));
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

// A human-readable name for an unhandled exception's default-action message:
// the messageText if present, otherwise the exception's class name.
std::string defaultActionMessage(proto::ProtoContext* ctx,
                                  const proto::ProtoObject* exc) {
    std::string m = messageTextOf(ctx, exc);
    if (!m.empty()) return m;
    const proto::ProtoObject* cn = exc ? exc->getAttribute(ctx, classNameKey(ctx)) : nullptr;
    if (cn && cn != PROTO_NONE) {
        const proto::ProtoString* s = cn->asString(ctx);
        if (s) {
            std::string name = s->toStdString(ctx);
            if (!name.empty()) return name;
        }
    }
    return std::string("unhandled exception");
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
// EXC-d: the abort throw is UnhandledSTException, NOT a bare std::runtime_error.
// It derives from std::runtime_error so the `drainOne` / `runTopLevel`
// `catch (const std::exception&)` clauses keep working unchanged; the dedicated
// type lets the EXC-d native-translation wrapper recognise an
// already-protoST exception and re-throw it instead of double-translating it.
const proto::ProtoObject* defaultAction(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* exc) {
    if (!isResumable(ctx, exc)) {
        // Error / non-resumable: abort the activation (EXC-a behaviour, EXC-d
        // dedicated type).
        throw UnhandledSTException(defaultActionMessage(ctx, exc));
    }
    // Resumable and unhandled. A Warning announces itself; the bare Exception
    // base resumes silently. The distinction is the presence of a messageText
    // worth reporting — print it for any resumable exception that carries one.
    {
        std::string m = messageTextOf(ctx, exc);
        if (!m.empty()) {
            std::fputs("Warning: ", stderr);
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
const proto::ProtoObject* signalInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exc) {
    // Pin the instance for the whole primitive — it is held across handler-
    // stack walks and invokeBlock (which spins a full nested engine).
    TransientPin pinExc(ctx, exc);

    unsigned long searchBelowId = 0;   // 0 == search from the top of the stack
    for (;;) {
        const HandlerEntry* entry = handlerStackFindMatch(ctx, exc, searchBelowId);
        if (!entry) {
            // No (further) matching handler: run the exception's default
            // action. For a resumable exception this returns a value that
            // `signal` yields; for an Error it throws.
            return defaultAction(ctx, exc);
        }

        unsigned long handlerId          = entry->handlerId;
        const proto::ProtoObject* hBlock = entry->handlerBlock;

        // Stamp the active handler id onto the instance so the handler
        // actions (`return:`, `resume:`, `retry`, `pass`) — which only
        // receive the exception as their receiver — can reach this entry.
        const_cast<proto::ProtoObject*>(exc)->setAttribute(
            ctx, activeHandlerKey(ctx),
            ctx->fromLong(static_cast<long long>(handlerId)));

        // Disable this entry and every inner one, so a `signal` raised while
        // the handler block runs is caught by an OUTER handler.
        std::vector<unsigned long> disabled = handlerStackDisableFrom(handlerId);

        const proto::ProtoObject* handlerResult = nullptr;
        try {
            const proto::ProtoObject* a0 = exc;
            handlerResult = invokeBlock(rt, ctx, hBlock, &a0, 1);
        } catch (const ResumeSignal& r) {
            // `resume: v` — only ours is consumed here; an inner id belongs
            // to an outer signal loop.
            if (r.handlerId() != handlerId) { handlerStackRestore(disabled); throw; }
            handlerStackRestore(disabled);
            // `signal` returns v: the protected block (its stack never
            // unwound) continues from the signal point.
            return r.value() ? r.value() : PROTO_NONE;
        } catch (const PassSignal& p) {
            // `pass` / `outer` — search outward for the next matching handler.
            if (p.handlerId() != handlerId) { handlerStackRestore(disabled); throw; }
            handlerStackRestore(disabled);
            searchBelowId = handlerId;   // resume search strictly outer to this
            continue;
        } catch (...) {
            // UnwindToHandler (return:), RetrySignal (retry), NonLocalReturn,
            // a cooperative yield, or another error. Re-enable what we
            // disabled so the handler stack stays consistent for outer
            // handlers / the owning `on:do:`, then let the throw continue.
            handlerStackRestore(disabled);
            throw;
        }

        // The handler fell off its end without return:/resume:/retry/pass.
        // Fall-through == `return: handlerResult`.
        handlerStackRestore(disabled);
        throw UnwindToHandler{ handlerId,
                               handlerResult ? handlerResult : PROTO_NONE };
    }
}

} // namespace

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

const proto::ProtoObject* signalNativeError(STRuntime& rt,
                                            proto::ProtoContext* ctx,
                                            const char* message) {
    return signalErrorOfClass(rt, ctx, rt.bootstrap().errorProto,
                              message ? message : "native exception");
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

// anException messageText → the stored string (nil when unset)
const proto::ProtoObject* prim_Exception_messageText(STRuntime&, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* r,
                                                      const proto::ProtoObject* const*, int) {
    const proto::ProtoObject* m = r ? r->getAttribute(ctx, msgTextKey(ctx)) : nullptr;
    return (m && m != PROTO_NONE) ? m : PROTO_NONE;
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
// target handler id was stamped onto the instance by `signal`.
const proto::ProtoObject* prim_Exception_return(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 1) throw std::runtime_error("return: expects 1 arg (value)");
    const proto::ProtoObject* idObj = r ? r->getAttribute(ctx, activeHandlerKey(ctx)) : nullptr;
    if (!idObj || idObj == PROTO_NONE) {
        // `return:` on an exception that is not currently being handled.
        throw std::runtime_error("return: sent to an exception with no active handler");
    }
    unsigned long handlerId = static_cast<unsigned long>(idObj->asLong(ctx));
    throw UnwindToHandler{ handlerId, a[0] ? a[0] : PROTO_NONE };
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
    unsigned long handlerId = activeHandlerIdOf(ctx, r);
    if (handlerId == 0)
        throw std::runtime_error("resume: sent to an exception with no active handler");
    // `resume` with no argument == `resume: nil`.
    const proto::ProtoObject* v = (argc == 1 && a[0]) ? a[0] : PROTO_NONE;
    throw ResumeSignal{ handlerId, v };
}

// anException retry
//
// Handler action: unwind to the owning `on:do:` and re-evaluate the protected
// block from scratch. Propagates PAST `signal` to the `on:do:` primitive.
const proto::ProtoObject* prim_Exception_retry(STRuntime&, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const*,
                                                int) {
    unsigned long handlerId = activeHandlerIdOf(ctx, r);
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
    unsigned long handlerId = activeHandlerIdOf(ctx, r);
    if (handlerId == 0)
        throw std::runtime_error("pass sent to an exception with no active handler");
    throw PassSignal{ handlerId };
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
const proto::ProtoObject* runProtected(
        STRuntime& rt, proto::ProtoContext* ctx,
        const proto::ProtoObject* protectedBlock,
        const std::vector<std::pair<const proto::ProtoObject*,
                                    const proto::ProtoObject*>>& guards) {
    for (;;) {   // each turn is one attempt; `retry` loops back here
        std::vector<unsigned long> ids;
        ids.reserve(guards.size());
        for (const auto& g : guards)
            ids.push_back(handlerStackPush(g.first, g.second));

        auto popAll = [&]() {
            // Idempotent; pop newest-first so the stack stays well-formed.
            for (std::size_t i = ids.size(); i-- > 0; )
                handlerStackPop(ids[i]);
        };
        auto owns = [&](unsigned long id) {
            for (unsigned long x : ids) if (x == id) return true;
            return false;
        };

        try {
            const proto::ProtoObject* result =
                invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
            popAll();
            return result;
        } catch (const UnwindToHandler& u) {
            popAll();
            if (owns(u.handlerId()))
                return u.value() ? u.value() : PROTO_NONE;
            throw;   // targets an OUTER construct
        } catch (const RetrySignal& r) {
            popAll();
            if (owns(r.handlerId()))
                continue;   // re-evaluate the protected block — loop again
            throw;          // targets an OUTER construct
        } catch (...) {
            // NonLocalReturn, FutureYield, ResumeSignal/PassSignal escaping a
            // bug, std::exception — all must leave the handler stack balanced.
            popAll();
            throw;
        }
    }
}

// protectedBlock on: GuardClass do: handlerBlock
const proto::ProtoObject* prim_Block_on_do(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* protectedBlock,
                                            const proto::ProtoObject* const* a,
                                            int argc) {
    if (argc != 2)
        throw std::runtime_error("on:do: expects 2 args (guard class, handler block)");
    std::vector<std::pair<const proto::ProtoObject*, const proto::ProtoObject*>> guards;
    guards.emplace_back(a[0], a[1]);
    return runProtected(rt, ctx, protectedBlock, guards);
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
// Re-throw correctness: the `catch (...)` branch runs the cleanup and then
// re-throws the ORIGINAL exception with a bare `throw;`, so an
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
    try {
        const proto::ProtoObject* result =
            invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
        // Normal exit → run the cleanup; its value is discarded.
        invokeBlock(rt, ctx, cleanupBlock, nullptr, 0);
        return result;
    } catch (const FutureYield&) {
        // A yield is a suspension, not an exit — propagate without cleanup.
        throw;
    } catch (...) {
        // Abnormal exit (UnwindToHandler / RetrySignal / NonLocalReturn /
        // std::exception) → run the cleanup, then re-propagate the original.
        invokeBlock(rt, ctx, cleanupBlock, nullptr, 0);
        throw;
    }
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
    try {
        const proto::ProtoObject* result =
            invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
        // Normal exit → cleanup SKIPPED.
        return result;
    } catch (const FutureYield&) {
        // A yield is a suspension, not a curtailment — propagate, no cleanup.
        throw;
    } catch (...) {
        // Abnormal exit only → run the cleanup, then re-propagate.
        invokeBlock(rt, ctx, cleanupBlock, nullptr, 0);
        throw;
    }
}

} // namespace

void installExceptionPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();

    // signal / signal: — bound on Exception, inherited by Error / Warning and
    // by user subclasses. Works both class-side (`Error signal: 'x'`) and
    // instance-side (`anError signal`).
    int signalIdx     = reg.registerPrim(prim_Exception_signal);
    int signalTextIdx = reg.registerPrim(prim_Exception_signalText);
    bindPrimitive(rt, b.exceptionProto, "signal",  signalIdx);
    bindPrimitive(rt, b.exceptionProto, "signal:", signalTextIdx);

    // Accessors + the handler actions.
    bindPrimitive(rt, b.exceptionProto, "messageText",
                  reg.registerPrim(prim_Exception_messageText));
    bindPrimitive(rt, b.exceptionProto, "messageText:",
                  reg.registerPrim(prim_Exception_setMessageText));
    bindPrimitive(rt, b.exceptionProto, "return:",
                  reg.registerPrim(prim_Exception_return));

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
