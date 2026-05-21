#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace protoST {

// invokeBlock and blockArgCount are defined in block_prims.cpp; the forward
// declarations let the nil-test primitives evaluate their argument blocks and
// dispatch on block arity through the single shared `__bc_ptr__` code path.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc);
int blockArgCount(proto::ProtoContext* ctx, const proto::ProtoObject* block);

namespace {

// D9: evaluate `block`, passing the receiver `r` to it when it is a one-arg
// block and nothing when it is a zero-arg block. `ifNotNil:` / `ifNil:ifNotNil:`
// thus accept either arity (modern `[:x| …]` or classic `[…]`).
const proto::ProtoObject* invokeNilTestBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* block,
                                              const proto::ProtoObject* r) {
    if (blockArgCount(ctx, block) >= 1) {
        const proto::ProtoObject* args[1] = { r };
        return invokeBlock(rt, ctx, block, args, 1);
    }
    return invokeBlock(rt, ctx, block, nullptr, 0);
}

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

// class __installClassMethod: methodObj as: selectorSym
//   → stamp methodObj with `__class_side__ = true`
//   → setAttribute(class, selectorSym, methodObj)
//   → returns class (recv)
//
// D5 (MNT-b2): the class-side counterpart of `__installMethod:as:`. The method
// still lands on the SAME class object, but the `__class_side__` marker on the
// wrapper lets the engine's SEND dispatch refuse it when the receiver is an
// instance — so `ClassName class >> sel` is reachable from the class object and
// NOT from its instances.
const proto::ProtoObject* prim_Object_installClassMethod(STRuntime&,
                                                         proto::ProtoContext* ctx,
                                                         const proto::ProtoObject* r,
                                                         const proto::ProtoObject* const* a,
                                                         int argc) {
    if (argc != 2)
        throw std::runtime_error("__installClassMethod:as: expects 2 args");
    auto* selStr = a[1]->asString(ctx);
    if (!selStr)
        throw std::runtime_error(
            "__installClassMethod:as: selector must be a symbol");
    const proto::ProtoString* classSideKey =
        proto::ProtoString::createSymbol(ctx, "__class_side__");
    const_cast<proto::ProtoObject*>(a[0])->setAttribute(
        ctx, classSideKey, PROTO_TRUE);
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
    // Resolve the symbol fresh from the live ctx — symbols are interned
    // per-ProtoSpace, so a function-local `static` would stamp later runtimes'
    // classes under a stale key from the first runtime's space. The D5
    // class/instance-side test reads this attribute back via `__class_name__`;
    // a stale key here would make every class look like a non-class.
    const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, nameKey, a[0]);
    return r;
}

// --- T3-a: subclass: as a runtime message ----------------------------------
//
// The compiler recognises the textual form `Identifier subclass: #Name …` and
// desugars it to `newChild` + `__setClassName:` (see Compiler::emitStatement).
// That textual form ONLY fires when the receiver is a bare identifier — it can
// not subclass a class reached through an expression, e.g. an imported module
// class: `(lib Counter) subclass: #FastCounter …`.
//
// These primitives make `subclass:` a genuine message on every object, so any
// class object — wherever it came from, including across a module boundary —
// can be subclassed. The new class is a fresh mutable child of the receiver
// (its prototype parent), stamped with its class name. Method lookup, `super`,
// and instance-variable access all walk the parent chain by object identity,
// so the parent living in another module is irrelevant.
//
// Like the compiler's textual form, the new class is also bound into globals
// under its name — so `lib Counter subclass: #FastCounter …` followed by
// `FastCounter >> incrementBy: …` resolves `FastCounter` (a PUSH_GLOBAL emitted
// by the MethodDecl path) with no separate assignment required. The primitive
// still returns the class, so an explicit `Sub := … subclass: …` also works.

namespace {
// Shared helper: create a named subclass of `superCls` and bind it as a global.
const proto::ProtoObject* makeSubclass(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* superCls,
                                       const proto::ProtoObject* nameArg) {
    if (!superCls || superCls == PROTO_NONE)
        throw std::runtime_error("subclass:: superclass is nil");
    auto* nameStr = nameArg ? nameArg->asString(ctx) : nullptr;
    if (!nameStr)
        throw std::runtime_error(
            "subclass:: class name must be a symbol or string");
    auto* sub = superCls->newChild(ctx, /*isMutable=*/true);
    TransientPin pinSub(ctx, sub);
    const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");
    const_cast<proto::ProtoObject*>(sub)->setAttribute(
        ctx, nameKey, nameStr->asObject(ctx));
    // Bind the class globally under its declared name, mirroring the textual
    // `subclass:` form's STORE_GLOBAL. The class name is a symbol/string; the
    // global key is its interned-symbol form.
    std::string name = nameStr->toStdString(ctx);
    auto* nameSym = proto::ProtoString::createSymbol(ctx, name.c_str());
    if (auto* g = rt.globals()) g->setAttribute(ctx, nameSym, sub);
    return sub;
}
} // namespace

// superClass subclass: #Name
//   → fresh mutable child of superClass, stamped with the class name.
const proto::ProtoObject* prim_Object_subclass(STRuntime& rt,
                                               proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    if (argc != 1) throw std::runtime_error("subclass: expects 1 arg");
    return makeSubclass(rt, ctx, r, a[0]);
}

// superClass subclass: #Name instanceVariableNames: 'a b c'
//   → same as subclass:, plus the instance-variable names string is accepted.
//
// Instance variables in protoST are plain attributes resolved by symbol on
// `self` (STORE_INSTVAR / PUSH_INSTVAR walk the parent chain), so no slot
// layout has to be reserved here — declaring them is informational. The names
// string is validated for shape and otherwise carried only so the surface
// syntax matches the textual `subclass:instanceVariableNames:` form.
const proto::ProtoObject* prim_Object_subclassIvars(
    STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* r,
    const proto::ProtoObject* const* a, int argc) {
    if (argc != 2)
        throw std::runtime_error(
            "subclass:instanceVariableNames: expects 2 args");
    // a[1] is the instance-variable-names string; accept any string (incl. '').
    if (a[1] && a[1] != PROTO_NONE && !a[1]->asString(ctx))
        throw std::runtime_error(
            "subclass:instanceVariableNames:: ivar names must be a string");
    return makeSubclass(rt, ctx, r, a[0]);
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

// --- D9: nil-test protocol on Object ---------------------------------------
//
// `nil` is PROTO_NONE; every other object is non-nil. These primitives are
// bound on objectProto so every object — nil included, since nilProto's parent
// is objectProto — answers them.

const proto::ProtoObject* prim_Object_isNil(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    return (r == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_Object_notNil(STRuntime&, proto::ProtoContext*,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    return (r == PROTO_NONE) ? PROTO_FALSE : PROTO_TRUE;
}

// `ifNil:` — evaluate the block when the receiver is nil, else answer the
// receiver. `ifNotNil:` — evaluate the block (optionally passed the receiver)
// when the receiver is non-nil, else answer nil.
const proto::ProtoObject* prim_Object_ifNil(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a, int) {
    if (r == PROTO_NONE) return invokeBlock(rt, ctx, a[0], nullptr, 0);
    return r;
}
const proto::ProtoObject* prim_Object_ifNotNil(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a, int) {
    if (r == PROTO_NONE) return PROTO_NONE;
    return invokeNilTestBlock(rt, ctx, a[0], r);
}
// `ifNil:ifNotNil:` — a[0] is the nil arm (0-arg), a[1] the non-nil arm
// (0-arg or 1-arg). Exactly one arm runs.
const proto::ProtoObject* prim_Object_ifNilIfNotNil(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const* a, int) {
    if (r == PROTO_NONE) return invokeBlock(rt, ctx, a[0], nullptr, 0);
    return invokeNilTestBlock(rt, ctx, a[1], r);
}

// --- D18: identity and default equality on Object --------------------------
//
// `==` is pointer identity; `~~` its negation. `=` defaults to identity (a
// value-equality override on a subtype — SmallInteger, String, … — shadows
// it); `~=` is `(self = arg) not`, routed through the receiver's own `=` so an
// override is honoured.

const proto::ProtoObject* prim_Object_identEq(STRuntime&, proto::ProtoContext*,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a, int) {
    return (r == a[0]) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_Object_identNe(STRuntime&, proto::ProtoContext*,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a, int) {
    return (r != a[0]) ? PROTO_TRUE : PROTO_FALSE;
}
// Default `=` — identity. Overridden on value types.
const proto::ProtoObject* prim_Object_eq(STRuntime&, proto::ProtoContext*,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int) {
    return (r == a[0]) ? PROTO_TRUE : PROTO_FALSE;
}
// Default `~=` — the negation of identity `=`. Value types that override `=`
// (SmallInteger, String) also bind their own `~=`, so this is reached only by
// objects whose `=` is the default identity comparison.
const proto::ProtoObject* prim_Object_ne(STRuntime&, proto::ProtoContext*,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int) {
    return (r != a[0]) ? PROTO_TRUE : PROTO_FALSE;
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
    bindPrimitive(rt, b.objectProto, "__installClassMethod:as:",
                  reg.registerPrim(prim_Object_installClassMethod));
    bindPrimitive(rt, b.objectProto, "asActor",
                  reg.registerPrim(prim_Object_asActor));
    // BL-3: rich printString. `__setClassName:` is emitted by the compiler's
    // ClassDecl path; `printString` is the default inherited by every object.
    bindPrimitive(rt, b.objectProto, "__setClassName:",
                  reg.registerPrim(prim_Object_setClassName));
    // T3-a: `subclass:` as a runtime message so any class object — including
    // one imported from a module — can be subclassed via an expression
    // receiver, not just the compiler's textual `Identifier subclass: …` form.
    bindPrimitive(rt, b.objectProto, "subclass:",
                  reg.registerPrim(prim_Object_subclass));
    bindPrimitive(rt, b.objectProto, "subclass:instanceVariableNames:",
                  reg.registerPrim(prim_Object_subclassIvars));
    bindPrimitive(rt, b.objectProto, "printString",
                  reg.registerPrim(prim_Object_printString));
    // F6 v2 T6: sleep primitive — test-only helper for the wall-clock
    // parallelism proof. Bound on objectProto so any object responds to it.
    bindPrimitive(rt, b.objectProto, "sleep:",
                  reg.registerPrim(prim_Object_sleepMs));
    // D9: nil-test protocol — bound on Object so every object (nil included,
    // since nilProto descends from objectProto) answers it.
    bindPrimitive(rt, b.objectProto, "isNil",
                  reg.registerPrim(prim_Object_isNil));
    bindPrimitive(rt, b.objectProto, "notNil",
                  reg.registerPrim(prim_Object_notNil));
    bindPrimitive(rt, b.objectProto, "ifNil:",
                  reg.registerPrim(prim_Object_ifNil));
    bindPrimitive(rt, b.objectProto, "ifNotNil:",
                  reg.registerPrim(prim_Object_ifNotNil));
    bindPrimitive(rt, b.objectProto, "ifNil:ifNotNil:",
                  reg.registerPrim(prim_Object_ifNilIfNotNil));
    // D18: identity comparison and default equality, bound on Object so every
    // object answers them. `=` here is the identity default; value-equality
    // overrides on SmallInteger / String shadow it on those types.
    bindPrimitive(rt, b.objectProto, "==",
                  reg.registerPrim(prim_Object_identEq));
    bindPrimitive(rt, b.objectProto, "~~",
                  reg.registerPrim(prim_Object_identNe));
    bindPrimitive(rt, b.objectProto, "=",
                  reg.registerPrim(prim_Object_eq));
    bindPrimitive(rt, b.objectProto, "~=",
                  reg.registerPrim(prim_Object_ne));
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
