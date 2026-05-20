// Track 1, slice 2 (EXC-a): the exception protocol primitives.
//
// Implements the class hierarchy accessors, `signal` / `signal:`, the
// protected-block primitive `on:do:`, and the handler action `return:`.
// Resumption (`resume:`), `retry` and `pass` are EXC-b; `ensure:` /
// `ifCurtailed:` are EXC-c; native C++ exception translation is EXC-d.
//
// Control-flow summary:
//
//   * `on:do:` (on blockProto) pushes a HandlerEntry, runs the protected
//     block via invokeBlock, and pops the entry on EVERY exit path. It owns
//     a unique handlerId; an UnwindToHandler carrying that id is this
//     `on:do:`'s signal to return the carried value.
//
//   * `signal` walks the thread-local handler stack for the newest enabled
//     matching entry. With NO matching handler it throws a std::runtime_error
//     (the EXC-a default action — REPL/-e prints it, an actor rejects its
//     Future). With a match it stamps the handler id onto the exception
//     instance, disables that entry plus every inner one (so a signal inside
//     the handler escapes outward), runs the handler block IN PLACE, and —
//     on fall-through — throws UnwindToHandler{ handlerId, handlerResult }.
//
//   * `return:` throws UnwindToHandler{ activeHandlerId(self), v } — the
//     explicit form of handler fall-through, reaching the same `on:do:`.

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/HandlerStack.h"
#include "runtime/UnwindToHandler.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>

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

// --- Core: signal an exception INSTANCE ------------------------------------
//
// Shared by instance-side `signal` and class-side `signal` / `signal:`.
const proto::ProtoObject* signalInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exc) {
    // Pin the instance for the whole primitive — it is held across handler-
    // stack walks and invokeBlock (which spins a full nested engine).
    TransientPin pinExc(ctx, exc);

    const HandlerEntry* entry = handlerStackFindMatch(ctx, exc);
    if (!entry) {
        // EXC-a default action: no matching handler. Abort the current
        // activation with the message — an actor rejects its Future, the
        // REPL/-e prints it. (Warning's print-and-resume default is EXC-b.)
        throw std::runtime_error(defaultActionMessage(ctx, exc));
    }

    unsigned long handlerId          = entry->handlerId;
    const proto::ProtoObject* hBlock = entry->handlerBlock;

    // Stamp the active handler id onto the instance so `return:` (which only
    // receives the exception as its receiver) can reach this same `on:do:`.
    const_cast<proto::ProtoObject*>(exc)->setAttribute(
        ctx, activeHandlerKey(ctx), ctx->fromLong(static_cast<long long>(handlerId)));

    // Disable this entry and every inner one, so a `signal` raised while the
    // handler block runs is caught by an OUTER handler — not by this `on:do:`
    // again and not by a handler nested inside the protected block.
    std::vector<unsigned long> disabled = handlerStackDisableFrom(handlerId);

    // Run the handler block IN PLACE with the signalling stack intact,
    // passing the exception instance as its sole argument.
    const proto::ProtoObject* handlerResult = nullptr;
    try {
        const proto::ProtoObject* a0 = exc;
        handlerResult = invokeBlock(rt, ctx, hBlock, &a0, 1);
    } catch (...) {
        // The handler did `return:` (UnwindToHandler), a non-local return,
        // a cooperative yield, or raised another error. Re-enable what we
        // disabled so the handler stack is consistent for outer handlers,
        // then let the throw continue to its target.
        handlerStackRestore(disabled);
        throw;
    }

    // The handler fell off its end without return:/resume:/etc.
    // EXC-a: handler fall-through == `return: handlerResult`.
    handlerStackRestore(disabled);
    throw UnwindToHandler{ handlerId, handlerResult ? handlerResult : PROTO_NONE };
}

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

// protectedBlock on: GuardClass do: handlerBlock
//
// Pushes a HandlerEntry, runs the protected block, and pops the entry on
// every exit path. An UnwindToHandler whose id matches this `on:do:`'s entry
// means "yield this value"; any other UnwindToHandler is for an outer
// `on:do:` and is re-thrown.
const proto::ProtoObject* prim_Block_on_do(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* protectedBlock,
                                            const proto::ProtoObject* const* a,
                                            int argc) {
    if (argc != 2)
        throw std::runtime_error("on:do: expects 2 args (guard class, handler block)");
    const proto::ProtoObject* guardClass   = a[0];
    const proto::ProtoObject* handlerBlock = a[1];

    unsigned long handlerId = handlerStackPush(guardClass, handlerBlock);
    try {
        const proto::ProtoObject* result = invokeBlock(rt, ctx, protectedBlock, nullptr, 0);
        handlerStackPop(handlerId);
        return result;
    } catch (const UnwindToHandler& u) {
        // popHandlerEntry is idempotent — the entry may already be gone if a
        // nested path removed it. Ensure it is removed regardless.
        handlerStackPop(handlerId);
        if (u.handlerId() == handlerId) {
            // This `on:do:` is the unwind target. Yield the carried value.
            return u.value() ? u.value() : PROTO_NONE;
        }
        // Targets an OUTER on:do: — keep propagating.
        throw;
    } catch (...) {
        // Any other exit (NonLocalReturn, FutureYield, std::exception, ...)
        // must still leave the handler stack balanced.
        handlerStackPop(handlerId);
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

    // Accessors + the `return:` handler action.
    bindPrimitive(rt, b.exceptionProto, "messageText",
                  reg.registerPrim(prim_Exception_messageText));
    bindPrimitive(rt, b.exceptionProto, "messageText:",
                  reg.registerPrim(prim_Exception_setMessageText));
    bindPrimitive(rt, b.exceptionProto, "return:",
                  reg.registerPrim(prim_Exception_return));

    // on:do: — the protected-block primitive, on blockProto.
    bindPrimitive(rt, b.blockProto, "on:do:",
                  reg.registerPrim(prim_Block_on_do));
}

} // namespace protoST
