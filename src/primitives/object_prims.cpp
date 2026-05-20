#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

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
    // F6 v3 E5: `actor` is a fresh mutable object held in a C++ local across
    // every line of this primitive — four setAttribute calls on a mutable
    // object (each allocates a sparse-list node), ctx->newList, ctx->fromLong,
    // ProtoString::createSymbol, and ctx->fromExternalPointer. Until it is
    // returned (and the caller pushes it onto a GC-traced operand slot) it is
    // reachable from no traced root. Pin it for the primitive's lifetime.
    TransientPin pinActor(ctx, actor);

    // __wrapped__ = recv
    static const proto::ProtoString* wrappedKey =
        proto::ProtoString::createSymbol(ctx, "__wrapped__");
    actor->setAttribute(ctx, wrappedKey, r);

    // __mailbox__ = empty ProtoList (Lisp-style cons stack)
    static const proto::ProtoString* mailboxKey =
        proto::ProtoString::createSymbol(ctx, "__mailbox__");
    auto* emptyList = ctx->newList();
    // `emptyList` is held across asObject + setAttribute — pin it.
    TransientPin pinEmptyList(
        ctx, reinterpret_cast<const proto::ProtoObject*>(emptyList));
    actor->setAttribute(ctx, mailboxKey, emptyList->asObject(ctx));

    // __state__ = 0 (idle)
    static const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
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
    // `lockEP` is held across the final setAttribute — pin it.
    TransientPin pinLockEP(ctx, lockEP);
    actor->setAttribute(ctx, lockKey, lockEP);

    return actor;
}

// Object>>sleep:
//
// F6 v2 T6 test helper. Sleeps the current OS thread for the requested number
// of milliseconds and returns the receiver. Used exclusively by the
// wall-clock parallelism proof tests to make actor messages take an
// observable amount of real time without depending on Smalltalk-side busy
// loops (which would be sensitive to interpreter perf changes).
//
// This is a low-level helper meant for the test suite; it is intentionally
// not advertised in user-facing docs. It blocks the entire worker thread for
// the duration, which is exactly what the test needs to observe parallel
// execution on different workers.
const proto::ProtoObject* prim_Object_sleepMs(STRuntime&,
                                               proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    if (argc != 1) throw std::runtime_error("sleep: expects 1 arg (milliseconds)");
    long long ms = a[0]->asLong(ctx);
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    return r;
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

// class __setClassName: nameString
//   → setAttribute(class, #__class_name__, nameString)
//   → returns class (recv)
//
// BL-3: emitted by the compiler's ClassDecl path right after `newChild`, so
// every user class object carries its declared name. printString reads this
// attribute (walking the prototype chain) to render "a Counter" etc.
const proto::ProtoObject* prim_Object_setClassName(STRuntime&,
                                                    proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* r,
                                                    const proto::ProtoObject* const* a,
                                                    int argc) {
    if (argc != 1) throw std::runtime_error("__setClassName: expects 1 arg");
    static const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, nameKey, a[0]);
    return r;
}

// recv printString → human-readable ProtoString
//
// BL-3: default Object>>printString. Resolves the receiver's class name by
// reading the `__class_name__` attribute (getAttribute walks the prototype
// chain, so an instance finds the name stamped on its class object) and
// returns "a ClassName" — or "an ClassName" when the class name starts with a
// vowel. A user class can override this by defining its own `printString`
// method; ordinary inheritance handles that with no special-casing here.
//
// If the receiver IS a class object (it carries `__class_name__` as an OWN
// attribute), the bare class name is returned ("Counter") rather than
// "a Counter" — nicer for printing the class itself.
const proto::ProtoObject* prim_Object_printString(STRuntime&,
                                                   proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* r,
                                                   const proto::ProtoObject* const*,
                                                   int) {
    static const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");

    // An own `__class_name__` means the receiver is itself a class object.
    const proto::ProtoObject* ownName =
        r ? r->getOwnAttributeDirect(ctx, nameKey) : nullptr;
    if (ownName && ownName != PROTO_NONE) {
        auto* s = ownName->asString(ctx);
        if (s) return s->asObject(ctx);
    }

    // Otherwise walk the prototype chain for the class name.
    const proto::ProtoObject* nameObj =
        r ? r->getAttribute(ctx, nameKey) : nullptr;
    std::string name;
    if (nameObj && nameObj != PROTO_NONE) {
        auto* s = nameObj->asString(ctx);
        if (s) name = s->toStdString(ctx);
    }
    if (name.empty()) return ctx->fromUTF8String("an object");

    char c0 = name.empty() ? '\0' : name[0];
    bool vowel = (c0 == 'A' || c0 == 'E' || c0 == 'I' || c0 == 'O' || c0 == 'U');
    std::string out = (vowel ? "an " : "a ") + name;
    return ctx->fromUTF8String(out.c_str());
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
    // F6 v3 E5: `wrapper` is a fresh object held across the `exportsKey`
    // interning (first call allocates the symbol) and the getAttribute walk.
    // Pin it.
    TransientPin pinWrapper(ctx, wrapper);
    // Unwrap: getImportModule returns a wrapper with `exports` attribute
    // pointing to the module.
    static const proto::ProtoString* exportsKey =
        proto::ProtoString::createSymbol(ctx, "exports");
    auto* mod = wrapper->getAttribute(ctx, exportsKey);
    return (mod && mod != PROTO_NONE) ? mod : wrapper;
}

} // anon

void installObjectPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    {
        // BL-1: `newChild` and the conventional Smalltalk `new` both
        // allocate a fresh mutable child of the receiver. Class-side methods
        // such as `Counter class >> startingAt:` call `self new`; bind `new`
        // as an alias so that pattern resolves without forcing user code to
        // spell `newChild`.
        auto newChildPrim = reg.registerPrim(prim_Object_newChild);
        bindPrimitive(rt, b.objectProto, "newChild", newChildPrim);
        bindPrimitive(rt, b.objectProto, "new", newChildPrim);
    }
    bindPrimitive(rt, b.objectProto, "__installMethod:as:",
                  reg.registerPrim(prim_Object_installMethod));
    bindPrimitive(rt, b.objectProto, "asActor",
                  reg.registerPrim(prim_Object_asActor));
    // BL-3: rich printString. `__setClassName:` is emitted by the compiler's
    // ClassDecl path; `printString` is the default inherited by every object.
    bindPrimitive(rt, b.objectProto, "__setClassName:",
                  reg.registerPrim(prim_Object_setClassName));
    bindPrimitive(rt, b.objectProto, "printString",
                  reg.registerPrim(prim_Object_printString));
    // F6 v2 T6: sleep primitive — test-only helper for the wall-clock
    // parallelism proof. Bound on objectProto so any object responds to it.
    bindPrimitive(rt, b.objectProto, "sleep:",
                  reg.registerPrim(prim_Object_sleepMs));
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
    auto* importKey = proto::ProtoString::createSymbol(ctx, "Import");
    auto* g = rt.globals();
    g->setAttribute(ctx, importKey, importObj);
}

} // namespace protoST
