#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <mutex>
#include <stdexcept>

namespace protoST {

namespace {

// Object>>newChild
//
// Returns a fresh, *mutable* child of the receiver. Used by ClassDecl
// compilation (F4-U2) to allocate a new class prototype whose parent is the
// receiver — i.e. `Object subclass: #Foo` desugars to `Object newChild`.
const proto::ProtoObject* prim_Object_newChild(STRuntime&,
                                                proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const*,
                                                int) {
    return r->newChild(ctx, /*isMutable=*/true);
}

// recv asActor → new Actor wrapping recv
//
// F6-A2: Wraps any object as an Actor by creating a mutable child of
// actorProto with three attributes:
//   __wrapped__ : the original receiver
//   __mailbox__ : an empty (mutable) ProtoList serving as the cons-stack mailbox
//   __state__   : SmallInteger(0) — 0 = idle, non-zero values reserved for
//                 scheduler use (running / waiting) in later F6 tasks.
const proto::ProtoObject* prim_Object_asActor(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    auto& b = rt.bootstrap();
    // Create a new mutable child of actorProto.
    auto* actor = b.actorProto->newChild(ctx, /*isMutable=*/true);

    // __wrapped__ = recv
    static const proto::ProtoString* wrappedKey =
        ctx->fromUTF8String("__wrapped__")->asString(ctx);
    actor->setAttribute(ctx, wrappedKey, r);

    // __mailbox__ = empty ProtoList (Lisp-style cons stack)
    static const proto::ProtoString* mailboxKey =
        ctx->fromUTF8String("__mailbox__")->asString(ctx);
    auto* emptyList = ctx->newList();
    actor->setAttribute(ctx, mailboxKey, emptyList->asObject(ctx));

    // __state__ = 0 (idle)
    static const proto::ProtoString* stateKey =
        ctx->fromUTF8String("__state__")->asString(ctx);
    actor->setAttribute(ctx, stateKey, ctx->fromLong(0));

    // F6 v2 T3: per-actor mutex for thread-safe mailbox.
    //
    // The actor's mailbox is read-modify-written by two paths:
    //   * SEND fast-path in ExecutionEngine.cpp (foreground sender)
    //   * STRuntime::drainOne (worker thread or foreground Future>>wait)
    // setAttribute is individually atomic in protoCore but the RMW pair
    // (getAttribute + appendLast/getSlice + setAttribute) is not, so we
    // install a std::mutex per actor and the two call sites lock it for the
    // duration of the RMW. The mutex is heap-allocated and wrapped in an
    // ExternalPointer; the finalizer fires when the GC reclaims the actor
    // (at which point no thread can possibly hold the lock, since the actor
    // is unreachable).
    //
    // We use createSymbol("__lock__") because the key is permanent vocabulary
    // that every actor shares; a non-symbol ProtoString would needlessly be
    // re-interned on every actor creation. The pointer to the ProtoString is
    // cached in a function-local static so the symbol intern table is hit
    // exactly once per process for this key.
    static const proto::ProtoString* lockKey =
        proto::ProtoString::createSymbol(ctx, "__lock__");
    std::mutex* lockPtr = new std::mutex();
    auto* lockEP = ctx->fromExternalPointer(
        lockPtr,
        [](void* p) { delete static_cast<std::mutex*>(p); });
    actor->setAttribute(ctx, lockKey, lockEP);

    return actor;
}

// class __installMethod: methodObj as: selectorSym
//   → setAttribute(class, selectorSym, methodObj)
//   → returns class (recv)
//
// Used by MethodDecl compilation (F4-U3) to attach a compiled method wrapper
// (a BlockClosure-shaped object) to a class proto under its selector symbol.
const proto::ProtoObject* prim_Object_installMethod(STRuntime&,
                                                     proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const* a,
                                                     int argc) {
    if (argc != 2) throw std::runtime_error("__installMethod:as: expects 2 args");
    // a[0] = methodObj (BlockClosure-shaped)
    // a[1] = selector symbol (ProtoString tagged as symbol)
    auto* selStr = a[1]->asString(ctx);
    if (!selStr) throw std::runtime_error("__installMethod:as: selector must be a symbol");
    r->setAttribute(ctx, selStr, a[0]);
    return r;
}

// Import from: 'foo'
//
// F5 v2-M2: Resolves 'foo' via protoCore's Unified Module Discovery
// (getImportModule) — which consults SharedModuleCache and walks the registered
// provider chain (STModuleProvider, installed in M1). The receiver is the
// Import singleton object itself — only used as a dispatch site.
//
// getImportModule returns a wrapper object whose `exports` attribute points to
// the actual module ProtoObject loaded by STModuleProvider. We unwrap that so
// Smalltalk code sees the module directly (and can do `m Counter newChild`).
const proto::ProtoObject* prim_Import_from(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* /*recv*/,
                                            const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("Import>>from: expects 1 arg (logical path string)");
    auto* arg = a[0];
    if (!arg) throw std::runtime_error("Import>>from: arg is null");
    auto* str = arg->asString(ctx);
    if (!str) throw std::runtime_error("Import>>from: arg must be a string");
    std::string logical = str->toStdString(ctx);

    auto* wrapper = rt.space()->getImportModule(ctx, logical.c_str(), "exports");
    if (!wrapper || wrapper == PROTO_NONE) {
        throw std::runtime_error("module not found: " + logical);
    }
    // Unwrap: getImportModule returns a wrapper with `exports` attribute
    // pointing to the module.
    static const proto::ProtoString* exportsKey =
        ctx->fromUTF8String("exports")->asString(ctx);
    auto* mod = wrapper->getAttribute(ctx, exportsKey);
    return (mod && mod != PROTO_NONE) ? mod : wrapper;
}

} // anon

void installObjectPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.objectProto, "newChild",
                  reg.registerPrim(prim_Object_newChild));
    bindPrimitive(rt, b.objectProto, "__installMethod:as:",
                  reg.registerPrim(prim_Object_installMethod));
    bindPrimitive(rt, b.objectProto, "asActor",
                  reg.registerPrim(prim_Object_asActor));
}

// F5-M3: Allocate the `Import` singleton (mutable child of objectProto), bind
// `from:` on it, and register it in globals so user code can write
// `m := Import from: 'foo'.`
void installImportGlobal(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    auto* ctx = rt.rootCtx();

    // Create the Import singleton — mutable child of objectProto.
    auto* importObj = const_cast<proto::ProtoObject*>(b.objectProto)
        ->newChild(ctx, /*isMutable=*/true);

    // Bind from: on it.
    int idx = reg.registerPrim(prim_Import_from);
    bindPrimitive(rt, importObj, "from:", idx);

    // Register in globals so PUSH_GLOBAL resolves `Import`.
    auto* importKey = ctx->fromUTF8String("Import")->asString(ctx);
    auto* g = rt.globals();
    g->setAttribute(ctx, importKey, importObj);
}

} // namespace protoST
