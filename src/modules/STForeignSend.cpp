#include "STForeignSend.h"
#include "STModuleProvider.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/HandlerStack.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/PrimitiveMarker.h"
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

// One SEND of `selector` to `recv`, with the engine's semantics: the class-side
// isolation (D5), doesNotUnderstand (D3), a user method, a primitive, a value
// attribute read by a unary send, and a protoCore method (a module's export).
const proto::ProtoObject* sendMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc) {
    const Bootstrap& bs = rt.bootstrap();
    const std::string selText = selector->toStdString(ctx);
    if (rt.isActor(ctx, recv))
        throw std::runtime_error("protoST: #" + selText +
                                 " was sent to an actor from another runtime, which is not "
                                 "supported: an actor's messages are asynchronous");
    const proto::ProtoObject* holder = (recv && recv != PROTO_NONE) ? recv : bs.nilProto;
    const proto::ProtoObject* attr = holder->getAttribute(ctx, selector);
    if (attr && attr != PROTO_NONE && !attr->isInteger(ctx) &&
        attr->getOwnAttributeDirect(ctx, bs.sym.classSide) == PROTO_TRUE) {
        const proto::ProtoObject* ownName = holder->getOwnAttributeDirect(ctx, bs.sym.className);
        if (!ownName || ownName == PROTO_NONE) attr = nullptr;  // an instance receiver
    }
    if (!attr || attr == PROTO_NONE) {
        if (canSendToForeignException(rt, ctx, recv, selector))
            return sendToForeignException(rt, ctx, recv, selector, args, argc);
        const proto::ProtoObject* message = makeMessage(rt, ctx, selText, args, argc);
        TransientPin pinMessage(ctx, message);
        const proto::ProtoString* dnuKey =
            proto::ProtoString::createSymbol(ctx, "doesNotUnderstand:");
        const proto::ProtoObject* dnu = holder->getAttribute(ctx, dnuKey);
        if (dnu && dnu != PROTO_NONE) {
            const proto::ProtoObject* bc = dnu->getOwnAttributeDirect(ctx, bs.sym.bcPtr);
            if (bc && bc != PROTO_NONE) {
                bool understood = false;
                const proto::ProtoObject* a1[1] = {message};
                return sendDynamic(rt, ctx, recv, dnuKey, a1, 1, &understood);
            }
        }
        const std::string text = "doesNotUnderstand: " + selText;
        return signalMessageNotUnderstood(rt, ctx, recv, message, text.c_str());
    }
    if (attr->isMethod(ctx))
        return recv->call(ctx, nullptr, selector, recv, args, static_cast<unsigned>(argc));
    const proto::ProtoObject* bc = attr->isInteger(ctx)
                                       ? nullptr
                                       : attr->getOwnAttributeDirect(ctx, bs.sym.bcPtr);
    const bool primitive = attr->isInteger(ctx) && isPrimitiveMarker(attr->asLong(ctx));
    if ((bc && bc != PROTO_NONE) || primitive) {
        bool understood = false;
        const proto::ProtoObject* r = sendDynamic(rt, ctx, recv, selector, args, argc, &understood);
        return r ? r : PROTO_NONE;
    }
    if (argc == 0) return attr;  // a value attribute, read by a unary send
    throw std::runtime_error("#" + selText +
                             ": the attribute is a value, not a method; only unary sends "
                             "read value attributes");
}

}  // namespace

const proto::ProtoObject* foreignSendEntry(proto::ProtoContext* callerCtx,
                                           const proto::ProtoObject* self,
                                           const proto::ParentLink*,
                                           const proto::ProtoArguments* positional,
                                           const proto::ProtoKeywordArguments* keywords) {
    if (!callerCtx || !callerCtx->space)
        throw std::runtime_error("protoST: a send needs the caller's context");
    STRuntime* rt = stRuntimeForSpace(callerCtx->space);
    const bool foreign = rt == nullptr;
    if (foreign) {
        // The provider's rule (Track Y): the runtime comes from protoST's own
        // state, never from the caller's space, and the ambiguous case is
        // refused rather than resolved arbitrarily.
        rt = soleSTRuntime();
        if (!rt)
            throw std::runtime_error(registeredSTRuntimeCount() > 1
                                         ? "protoST: a send from another runtime is ambiguous: "
                                           "this process holds more than one protoST runtime"
                                         : "protoST: a send from another runtime found no "
                                           "protoST runtime in this process");
        if (!rt->isOwnerThread())
            throw std::runtime_error(
                "protoST: a send from another runtime must run on the thread that "
                "constructed the protoST runtime");
    }
    if (keywords && keywords->getSize(callerCtx) > 0)
        throw std::runtime_error("protoST: a Smalltalk message takes no named arguments");
    const proto::proto_ulong n = positional ? positional->getSize(callerCtx) : 0;
    if (n < 1)
        throw std::runtime_error("protoST: " + std::string(kForeignSendSelector) +
                                 " expects the selector as its first argument");
    const proto::ProtoObject* selArg = positional->getAt(callerCtx, 0);
    const proto::ProtoString* selStr = selArg ? selArg->asString(callerCtx) : nullptr;
    if (!selStr)
        throw std::runtime_error("protoST: the selector of a send must be a String or a Symbol");

    // Where the message runs. A protoST caller keeps its own context. A foreign
    // one runs on protoST's current context on this thread -- the root context,
    // or the innermost one protoST code above it is running on -- because a
    // context's space is its parent's, so a child of the caller's context would
    // put protoST's allocations in the caller's space.
    proto::ProtoContext* ctx = callerCtx;
    if (foreign) {
        proto::ProtoContext* root = rt->rootCtx();
        proto::ProtoContext* current = root->thread ? root->thread->getCurrentContext() : nullptr;
        ctx = (current && current->space == root->space) ? current : root;
    }

    const proto::ProtoString* selector =
        proto::ProtoString::createSymbol(ctx, selStr->toStdString(callerCtx).c_str());
    std::vector<const proto::ProtoObject*> args;
    args.reserve(static_cast<size_t>(n - 1));
    for (proto::proto_ulong i = 1; i < n; ++i) {
        const proto::ProtoObject* a = positional->getAt(callerCtx, static_cast<int>(i));
        args.push_back(a ? a : PROTO_NONE);
    }
    const int argc = static_cast<int>(args.size());
    const proto::ProtoObject* recv = self ? self : PROTO_NONE;

    if (!foreign) return sendMessage(*rt, ctx, recv, selector, args.data(), argc);

    // The foreign boundary. The send is a native call boundary like a
    // primitive's: a C++ failure becomes a signalled Error, and with protoST's
    // outer handlers disabled an Error nothing inside the send handles leaves
    // as an UnhandledSTException. What leaves is decided in the catch clauses
    // and acted on after them (see translateNativeException).
    std::exception_ptr failed;
    bool controlFlowUnwind = false;
    const proto::ProtoObject* result = PROTO_NONE;
    {
        HandlerBarrier barrier;
        try {
            result = translateNativeException(*rt, ctx, [&]() -> const proto::ProtoObject* {
                return sendMessage(*rt, ctx, recv, selector, args.data(), argc);
            });
        } catch (const UnhandledSTException& u) {
            // A foreign exception that crossed protoST unhandled leaves as the
            // very exception that arrived (E3).
            //
            // A protoST Error no handler took leaves as the UnhandledSTException
            // that carries it -- unless the barrier hid handlers of protoST code
            // further up this thread (protoST called the foreign runtime, which
            // called this send). Those were never offered the Error, and an
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
        throw std::runtime_error(
            "protoST: #" + selector->toStdString(ctx) +
            " left through a control-flow unwind (a non-local return, a resumption, "
            "a retry, or a Future yield), which cannot cross a runtime boundary");
    return result ? result : PROTO_NONE;
}

void installForeignSendEntry(STRuntime& rt) {
    proto::ProtoContext* ctx = rt.rootCtx();
    const proto::ProtoObject* entry = ctx->fromMethod(nullptr, &foreignSendEntry);
    const_cast<proto::ProtoObject*>(rt.bootstrap().objectProto)
        ->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, kForeignSendSelector), entry);
}

} // namespace protoST
