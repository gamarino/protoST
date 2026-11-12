#include "runtime/ClassedErrorSignal.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/NativeError.h"
#include "runtime/ListIndex.h"
#include "STForeignSend.h"
#include "STModuleProvider.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BlockCode.h"
#include "runtime/HandlerStack.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

// Defined in object_prims.cpp: run a user method or a primitive by name, as a
// send does, in a nested engine on `ctx`.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

namespace {

// The handlers of the protoST code above a foreign frame, disabled for the
// duration of one foreign send (see the header) and restored on every exit.
class HandlerBarrier {
public:
    HandlerBarrier() : disabled_(handlerStackDisableAll()) {}
    ~HandlerBarrier() { handlerStackRestore(disabled_); }
    bool hidOuterHandlers() const { return !disabled_.empty(); }
    HandlerBarrier(const HandlerBarrier&) = delete;
    HandlerBarrier& operator=(const HandlerBarrier&) = delete;

private:
    std::vector<proto::proto_ulong> disabled_;
};

// One SEND of `selector` to `recv`, with the engine's semantics: the method
// holder (a class's metaclass, D52), doesNotUnderstand (D3), a user method, a primitive, a value
// attribute read by a unary send, and a protoCore method (a module's export).
const proto::ProtoObject* sendMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc) {
    const Bootstrap& bs = rt.bootstrap();
    const std::string selText = selector->toStdString(ctx);
    if (rt.isActor(ctx, recv))
        throw ClassedErrorSignal("Error", "protoST: #" + selText +
                                 " was sent to an actor from another runtime, which is not "
                                 "supported: an actor's messages are asynchronous");
    const proto::ProtoObject* holder = methodHolder(bs, ctx, recv);
    const proto::ProtoObject* attr = holder->getAttribute(ctx, selector);
    if (!attr || attr == PROTO_NONE) {
        if (canSendToForeignException(rt, ctx, recv, selector))
            return sendToForeignException(rt, ctx, recv, selector, args, argc);
        const proto::ProtoObject* message = makeMessage(rt, ctx, selText, args, argc);
        TransientPin pinMessage(ctx, message);
        const proto::ProtoString* dnuKey =
            proto::ProtoString::createSymbol(ctx, "doesNotUnderstand:");
        const proto::ProtoObject* dnu = holder->getAttribute(ctx, dnuKey);
        if (dnu && dnu != PROTO_NONE) {
            if (blockCodeOf(ctx, dnu->getOwnAttributeDirect(ctx, bs.sym.bcPtr))) {
                bool understood = false;
                const proto::ProtoObject* a1[1] = {message};
                return sendDynamic(rt, ctx, recv, dnuKey, a1, 1, &understood);
            }
        }
        const std::string text = "doesNotUnderstand: " + selText;
        return signalMessageNotUnderstood(rt, ctx, recv, message, text);
    }
    if (const proto::ProtoMethod native = attr->asMethod(ctx)) {
        // A primitive, or a native method another runtime exported: the
        // method cell is called directly, as a send in the engine calls it
        // (foreignSendEntry is the native boundary around this send).
        const proto::ProtoArguments positional(args, static_cast<unsigned>(argc));
        const proto::ProtoObject* r = callMethodCell(native, ctx, recv, &positional, nullptr);
        return r ? r : PROTO_NONE;
    }
    if (blockCodeOf(ctx, attr->getOwnAttributeDirect(ctx, bs.sym.bcPtr))) {
        bool understood = false;
        const proto::ProtoObject* r = sendDynamic(rt, ctx, recv, selector, args, argc, &understood);
        return r ? r : PROTO_NONE;
    }
    if (argc == 0) return attr;  // a value attribute, read by a unary send
    throw ClassedErrorSignal("Error", "#" + selText +
                             ": the attribute is a value, not a method; only unary sends "
                             "read value attributes");
}

}  // namespace

namespace {

// The protoST runtime a call from outside protoST's dispatch runs in, and
// whether the caller's space is a foreign one. The provider's rule (Track Y):
// for a foreign space the runtime comes from protoST's own state, never from
// the caller's space, and the ambiguous case is refused rather than resolved
// arbitrarily.
STRuntime& entryRuntime(proto::ProtoContext* callerCtx, bool& foreign) {
    if (!callerCtx || !callerCtx->space)
        throw NativeError("protoST: a send needs the caller's context");
    STRuntime* rt = stRuntimeForSpace(callerCtx->space);
    foreign = rt == nullptr;
    if (foreign) {
        rt = soleSTRuntime();
        if (!rt)
            throw NativeError(registeredSTRuntimeCount() > 1
                                         ? "protoST: a send from another runtime is ambiguous: "
                                           "this process holds more than one protoST runtime"
                                         : "protoST: a send from another runtime found no "
                                           "protoST runtime in this process");
        if (!rt->isOwnerThread())
            throw NativeError(
                "protoST: a send from another runtime must run on the thread that "
                "constructed the protoST runtime");
    }
    return *rt;
}

// Runs `body(rt, ctx)` behind protoST's native boundary: the one every call
// into protoST from outside its own dispatch crosses, whether it is a
// message sent through __st_send__ or a primitive's method cell called
// through protoCore's calling convention (primitiveMethod). `what` names the
// call in the error a control-flow unwind leaving it reports.
template <class Body>
const proto::ProtoObject* acrossBoundary(proto::ProtoContext* callerCtx, STRuntime& rt,
                                         bool foreign, const std::string& what, Body body) {
    // Where the call runs. A protoST caller keeps its own context. A foreign
    // one runs on protoST's current context on this thread -- the root context,
    // or the innermost one protoST code above it is running on -- because a
    // context's space is its parent's, so a child of the caller's context would
    // put protoST's allocations in the caller's space.
    proto::ProtoContext* ctx = callerCtx;
    if (foreign) {
        proto::ProtoContext* root = rt.rootCtx();
        proto::ProtoContext* current = root->thread ? root->thread->getCurrentContext() : nullptr;
        ctx = (current && current->space == root->space) ? current : root;
    }
    // K18: a context of protoST's space that is not protoST's own -- another
    // runtime's frame context, whose locals sit in its automaticLocals -- is
    // never run on directly; the call runs on a child context of it.
    EntryContext entry(rt, ctx);
    ctx = entry.ctx();

    // Every call that comes here crosses a runtime boundary: protoST's own
    // dispatch never does, so the caller is another runtime's code (a
    // foreign space, a host whose space protoST is a guest in, or a runtime
    // sharing protoST's space), with its frames between protoST code above and
    // this call. Until fix round 2 of K15 a caller in protoST's own space
    // skipped the boundary below, so a non-local return or a Future yield could
    // unwind through the caller's frames, and an outer protoST handler could
    // take an Error past them.

    // The foreign boundary. The call is a native call boundary like a
    // primitive's: a C++ failure becomes a signalled Error, and with protoST's
    // outer handlers disabled an Error nothing inside the call handles leaves
    // as an UnhandledSTException. What leaves is decided in the catch clauses
    // and acted on after them (see translateNativeException).
    std::exception_ptr failed;
    bool controlFlowUnwind = false;
    const proto::ProtoObject* result = PROTO_NONE;
    {
        HandlerBarrier barrier;
        try {
            result = translateNativeException(rt, ctx, [&]() -> const proto::ProtoObject* {
                return body(rt, ctx);
            });
        } catch (const UnhandledSTException& u) {
            // A foreign exception that crossed protoST unhandled leaves as the
            // very exception that arrived (E3).
            //
            // A protoST Error no handler took leaves as the UnhandledSTException
            // that carries it -- unless the barrier hid handlers of protoST code
            // further up this thread (protoST called the foreign runtime, which
            // called into protoST). Those were never offered the Error, and an
            // UnhandledSTException tells every protoST boundary it crosses that
            // they were, so it would pass them by. It leaves instead as a plain
            // ProtoException with the same value, which the boundary where the
            // foreign runtime returns into protoST signals again, as it does for
            // any language's error: the value, which is what every language
            // selects by, is unchanged; protoST's own description and trace are
            // computed again by the default action if nothing takes it there.
            if (u.original()) failed = u.original();
            else if (barrier.hidOuterHandlers())
                failed = std::make_exception_ptr(proto::ProtoException(ctx, u.value()));
            else failed = std::current_exception();
        } catch (const proto::ProtoException&) {
            failed = std::current_exception();
        } catch (const std::exception&) {
            failed = std::current_exception();
        } catch (...) {
            // NonLocalReturn, UnwindToHandler, RetrySignal, ResumeSignal,
            // PassSignal, FutureYield: each means "resume protoST somewhere up
            // the protoST stack", and the foreign caller's frames are in the way.
            controlFlowUnwind = true;
        }
    }
    if (failed) std::rethrow_exception(failed);
    if (controlFlowUnwind)
        throw NativeError(
            "protoST: " + what +
            " left through a control-flow unwind (a non-local return, a resumption, "
            "a retry, or a Future yield), which cannot cross a runtime boundary");
    return entry.leave(result ? result : PROTO_NONE);
}

}  // namespace

const proto::ProtoObject* foreignSendEntry(proto::ProtoContext* callerCtx,
                                           const proto::ProtoObject* self,
                                           const proto::ParentLink*,
                                           const proto::ProtoArguments* positional,
                                           const proto::ProtoKeywordArguments* keywords) {
    bool foreign = false;
    STRuntime& rt = entryRuntime(callerCtx, foreign);
    if (keywords && keywords->getSize(callerCtx) > 0)
        throw NativeError("protoST: a Smalltalk message takes no named arguments");
    const proto::proto_ulong n = positional ? positional->getSize(callerCtx) : 0;
    if (n < 1)
        throw NativeError("protoST: " + std::string(kForeignSendSelector) +
                                 " expects the selector as its first argument");
    const proto::ProtoObject* selArg = positional->getAt(callerCtx, 0);
    const proto::ProtoString* selStr = selArg ? selArg->asString(callerCtx) : nullptr;
    if (!selStr)
        throw NativeError("protoST: the selector of a send must be a String or a Symbol");
    const std::string selText = selStr->toStdString(callerCtx);
    std::vector<const proto::ProtoObject*> args;
    args.reserve(static_cast<size_t>(n - 1));
    for (proto::proto_ulong i = 1; i < n; ++i) {
        const proto::ProtoObject* a = positional->getAt(callerCtx, listIndex(i));
        args.push_back(a ? a : PROTO_NONE);
    }
    const proto::ProtoObject* recv = self ? self : PROTO_NONE;
    return acrossBoundary(callerCtx, rt, foreign, "#" + selText,
        [&](STRuntime& r, proto::ProtoContext* ctx) {
            const proto::ProtoString* selector =
                proto::ProtoString::createSymbol(ctx, selText.c_str());
            return sendMessage(r, ctx, recv, selector, args.data(),
                               static_cast<int>(args.size()));
        });
}

namespace detail {
const proto::ProtoObject* callPrimitiveFromOutside(proto::ProtoMethod primitive,
                                                   proto::ProtoContext* callerCtx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ProtoArguments* positional,
                                                   const proto::ProtoKeywordArguments* keywords) {
    bool foreign = false;
    STRuntime& rt = entryRuntime(callerCtx, foreign);
    const proto::ProtoObject* recv = self ? self : PROTO_NONE;
    return acrossBoundary(callerCtx, rt, foreign, "a primitive called through protoCore",
        [&](STRuntime&, proto::ProtoContext* ctx) {
            return callMethodCell(primitive, ctx, recv, positional, keywords);
        });
}
} // namespace detail

void installForeignSendEntry(STRuntime& rt) {
    proto::ProtoContext* ctx = rt.rootCtx();
    const proto::ProtoObject* entry = ctx->fromMethod(nullptr, &foreignSendEntry);
    const_cast<proto::ProtoObject*>(rt.bootstrap().objectProto)
        ->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, kForeignSendSelector), entry);
}

} // namespace protoST
