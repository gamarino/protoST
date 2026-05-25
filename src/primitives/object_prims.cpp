#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <chrono>
#include <stdexcept>
#include <string>
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
    // every line of this primitive — three setAttribute calls on a mutable
    // object (each allocates a sparse-list node), ctx->newList, ctx->fromLong
    // and ProtoString::createSymbol. Until it is returned (and the caller
    // pushes it onto a GC-traced operand slot) it is reachable from no traced
    // root. Pin it for the primitive's lifetime.
    TransientPin pinActor(ctx, actor);

    // __wrapped__ = recv
    const proto::ProtoString* wrappedKey =
        proto::ProtoString::createSymbol(ctx, "__wrapped__");
    actor->setAttribute(ctx, wrappedKey, r);

    // __mailbox__ = empty ProtoList (Lisp-style cons stack)
    const proto::ProtoString* mailboxKey =
        proto::ProtoString::createSymbol(ctx, "__mailbox__");
    auto* emptyList = ctx->newList();
    // `emptyList` is held across asObject + setAttribute — pin it.
    TransientPin pinEmptyList(
        ctx, reinterpret_cast<const proto::ProtoObject*>(emptyList));
    actor->setAttribute(ctx, mailboxKey, emptyList->asObject(ctx));

    // __state__ = 0 (idle)
    const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__state__");
    actor->setAttribute(ctx, stateKey, ctx->fromLong(0));

    // __sched__ = 0 — the lock-free scheduler's per-actor 3-state turn-
    // ownership flag (0 idle / 1 active / 2 active+wakeup-pending). It MUST
    // exist as SmallInteger 0 from birth so STRuntime::casSchedState's
    // 0->1 compare-and-swap has a concrete value to match against.
    actor->setAttribute(ctx, rt.bootstrap().sym.sched, ctx->fromLong(0));

    // No per-actor mutex and no scheduler mutex. The mailbox read-modify-write
    // (SEND fast-path / STRuntime::drainOne) and the scheduler itself are
    // lock-free over ProtoObject::setAttributeIfEqual — protoCore's atomic
    // attribute CAS. "At most one message in flight per actor" is enforced by
    // the __sched__ flag staying non-zero for the whole turn — see
    // STRuntime::drainOne / STRuntime::schedule / STRuntime::finishDrain.

    // F6 v5 (2026-05-23): attach a per-actor blocking lock for the new
    // task-list scheduler. A C++ binary_semaphore wrapped in an
    // ExternalPointer under __lockHandle__; drainOne acquires before
    // invoking the method and releases after. FIFO-fair via Linux futex
    // ordering, so two waiters on the same actor are released in the
    // order they popped their tasks from the global FIFO __tasks__ list.
    rt.attachActorLock(ctx, actor);

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
        // 2026-05-25: enter an unmanaged region for the duration of the
        // sleep so a concurrent GC stop-the-world phase does NOT wait
        // for this thread to reach a safepoint (it cannot — it is
        // suspended in the kernel until the timer fires). Without
        // this, a long `sleep:` on any worker stalls the entire GC
        // quorum for the full sleep duration. See protoCore DESIGN.md
        // §"Unmanaged regions" for the contract.
        proto::ProtoContext::UnmanagedScope u(ctx);
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

// --- T3-b: multiple inheritance / mixins -----------------------------------
//
// A mixin is just a class; "mixing in" means adding it as an additional
// parent. protoCore's prototype model supports several parents natively, and
// attribute/method lookup already walks the whole linearised parent chain, so
// protoST only adds the surface syntax (`uses:`) and assembles the chain.
//
// A protoCore object freezes its parent chain into its base cell at
// construction. `newChild` copies the base-cell chain of its prototype; an
// `addParent` on a mutable class mutates only the class's *snapshot*, which a
// child created by `newChild` never reads — so a parent added AFTER the class
// exists is invisible to that class's instances. Therefore a multiply-
// inheriting class must have its FULL parent chain in place before any
// instance is created.
//
// The class is assembled as a mutable `newChild` of an immutable "shape"
// object that carries the full linearised chain. The shape is built by
// applying protoCore's own `addParent` to an immutable cell — on an immutable
// object `addParent` returns a fresh cell with the parent (and its flattened
// ancestors) baked into the BASE chain, exactly what `newChild` later copies.
//
// Resolution order: the primary superclass subtree first, then each `uses:`
// mixin subtree in listed order. `addParent` prepends at the chain head, so
// the mixins are added in REVERSE listed order and the primary superclass is
// added last — leaving the head-first walk order
// [primary, primary-ancestors…, mixin0, mixin0-ancestors…, mixin1, …]. The
// diamond case (a selector reachable via two parents) resolves to the first
// in this order; `addParent` already de-duplicates shared ancestors.

namespace {
// Extract the backing ProtoList of the `uses:` collection. The collection is
// an Array-shaped object (its `__data__` attribute holds the ProtoList) —
// exactly what a `{ … }` dynamic-array literal evaluates to. A nil collection
// yields nullptr (no mixins).
const proto::ProtoList* mixinList(proto::ProtoContext* ctx,
                                  const proto::ProtoObject* mixinColl) {
    if (!mixinColl || mixinColl == PROTO_NONE) return nullptr;
    const proto::ProtoString* dataKey =
        proto::ProtoString::createSymbol(ctx, "__data__");
    const proto::ProtoObject* dataObj = mixinColl->getAttribute(ctx, dataKey);
    const proto::ProtoList* list =
        (dataObj && dataObj != PROTO_NONE) ? dataObj->asList(ctx)
                                           : mixinColl->asList(ctx);
    if (!list)
        throw std::runtime_error(
            "uses:: argument must be a collection of classes");
    return list;
}

// Build a named subclass of `superCls` that also inherits every class in the
// `uses:` collection, with the full prototype chain baked into the new class's
// base cell so instances see all parents. Stamps the class name and binds the
// class as a global, mirroring `makeSubclass`.
const proto::ProtoObject* makeSubclassWithMixins(
    STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* superCls,
    const proto::ProtoObject* nameArg, const proto::ProtoObject* mixinColl) {
    if (!superCls || superCls == PROTO_NONE)
        throw std::runtime_error("subclass:uses:: superclass is nil");
    auto* nameStr = nameArg ? nameArg->asString(ctx) : nullptr;
    if (!nameStr)
        throw std::runtime_error(
            "subclass:uses:: class name must be a symbol or string");
    const proto::ProtoList* mixins = mixinList(ctx, mixinColl);

    // Build the immutable "shape" carrying the full linearised parent chain.
    // `addParent` PREPENDS the new parent (and its flattened, de-duplicated
    // ancestors) at the chain head. To leave the head-first walk order
    //   [primary, primary-ancestors…, mixin0, mixin0-ancestors…, mixin1, …]
    // the parents are added LAST-WANTED-FIRST: each mixin in reverse listed
    // order, then the primary superclass last so it becomes the chain head.
    //
    // The shape starts from an immutable child of objectProto purely as a
    // construction anchor (objectProto is an ancestor of every class and gets
    // de-duplicated by `addParent` into its natural tail position).
    const proto::ProtoObject* shape =
        rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/false);
    TransientPin pinShape(ctx, shape);
    if (mixins) {
        for (long i = static_cast<long>(mixins->getSize(ctx)) - 1; i >= 0;
             --i) {
            const proto::ProtoObject* mixin =
                mixins->getAt(ctx, static_cast<int>(i));
            if (!mixin || mixin == PROTO_NONE)
                throw std::runtime_error("uses:: a mixin is nil");
            // addParent on an immutable cell returns a fresh immutable cell
            // with the parent baked into the base chain.
            shape = shape->addParent(ctx, mixin);
            pinShape.reset(shape);
        }
    }
    // Primary superclass added last → it becomes the chain head, so its
    // subtree is searched before any mixin subtree.
    shape = shape->addParent(ctx, superCls);
    pinShape.reset(shape);

    // The class object itself: a mutable child of the shape, so `>>` can
    // install methods in place. Its base parent is the shape, whose base
    // chain is the full linearised MRO — so `class new` instances inherit
    // every parent.
    auto* sub = shape->newChild(ctx, /*isMutable=*/true);
    TransientPin pinSub(ctx, sub);
    const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");
    const_cast<proto::ProtoObject*>(sub)->setAttribute(
        ctx, nameKey, nameStr->asObject(ctx));
    std::string name = nameStr->toStdString(ctx);
    auto* nameSym = proto::ProtoString::createSymbol(ctx, name.c_str());
    if (auto* g = rt.globals()) g->setAttribute(ctx, nameSym, sub);
    return sub;
}
} // namespace

// --- T3-c: on-the-fly behaviour composition --------------------------------
//
// `aClass addBehavior: aMixin` composes a behaviour (a mixin — just a class
// carrying methods) into a class at runtime, with no recompilation. After the
// call the class — and every instance created AFTER the call — responds to the
// mixin's methods.
//
// THE PROTOCORE CONSTRAINT (probed directly, see commit message / docs):
// protoCore freezes an object's parent chain into its BASE CELL at
// construction. `newChild` copies that frozen base chain; a later `addParent`
// or `setParents` on the (mutable) class mutates only the class object's own
// snapshot — which children created by `newChild` never read. Crucially this
// was found to hold even for instances created AFTER the mutation: a plain
// `aClass addParent: aMixin` is invisible to ALL of that class's instances,
// past and future. (Method *attributes* installed directly on the class via
// `>>` ARE seen by existing instances — lookup reaches the class object and
// reads its current own-attributes — but new *parents* are not.)
//
// Therefore `addBehavior:` cannot simply mutate the class. It REBUILDS the
// class as a fresh object whose base cell carries the mixin:
//
//   1. Take the old class's full (flattened) parent chain via `getParents`.
//   2. Build an immutable "shape" — `addParent` on an immutable cell bakes the
//      parent and its de-duplicated ancestors into the BASE chain — carrying
//      [old-parents…, aMixin], so the mixin is searched AFTER the existing
//      superclass/`uses:` subtrees (consistent with `uses:` resolution order).
//   3. The new class is a mutable `newChild` of that shape.
//   4. Copy every OWN attribute of the old class onto the new class — its
//      methods, `__class_name__`, class-side methods. The new class is thus a
//      single object carrying all the old behaviour PLUS the mixin in its base
//      chain, so `newChild` instances inherit everything.
//   5. Rebind the class's global name to the new object, so subsequent
//      `ClassName` references (every `PUSH_GLOBAL` re-resolves) and subsequent
//      `ClassName >> sel` method installs land on the rebuilt class.
//
// Because the rebuilt class is a single object (the old class is NOT kept in
// the chain), there is exactly one entry carrying the class's `__class_name__`
// — so `super` (which locates the defining class by walking the receiver's
// chain for a name match) stays correct for methods defined before OR after
// the `addBehavior:` call.
//
// DOCUMENTED LIMITATION — pre-existing instances. An instance created before
// `addBehavior:` froze its parent chain at its own construction; it keeps the
// old chain and does NOT gain the mixin. Lifting this would require a
// protoCore change to make `newChild`-frozen chains observe later parent
// mutations — out of scope for this slice. `addBehavior:` therefore has
// "future instances" semantics: it affects the class object and instances
// created after the call.

namespace {
// Rebuild `oldCls` as a fresh class object inheriting every parent of
// `oldCls` plus `mixin` (added last → searched after the existing parents),
// carrying a copy of every own attribute of `oldCls`. Rebinds the class's
// global name to the rebuilt object and returns it.
const proto::ProtoObject* addBehaviorToClass(STRuntime& rt,
                                             proto::ProtoContext* ctx,
                                             const proto::ProtoObject* oldCls,
                                             const proto::ProtoObject* mixin) {
    if (!oldCls || oldCls == PROTO_NONE)
        throw std::runtime_error("addBehavior:: receiver is nil");
    if (!mixin || mixin == PROTO_NONE)
        throw std::runtime_error("addBehavior:: behaviour (mixin) is nil");

    // Build the immutable shape carrying [old parents…, mixin]. `addParent`
    // PREPENDS at the chain head, so to leave the head-first walk order
    //   [old-parent0, old-parent1, …, mixin]
    // the mixin is added first (becomes the tail-most non-Object entry) and
    // the old parents are added in reverse so parent 0 ends at the head.
    const proto::ProtoObject* shape =
        rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/false);
    TransientPin pinShape(ctx, shape);
    shape = shape->addParent(ctx, mixin);
    pinShape.reset(shape);
    const proto::ProtoList* oldParents = oldCls->getParents(ctx);
    if (oldParents) {
        for (long i = static_cast<long>(oldParents->getSize(ctx)) - 1; i >= 0;
             --i) {
            const proto::ProtoObject* p =
                oldParents->getAt(ctx, static_cast<int>(i));
            if (!p || p == PROTO_NONE) continue;
            shape = shape->addParent(ctx, p);
            pinShape.reset(shape);
        }
    }

    // The rebuilt class: a mutable child of the shape, so `>>` can keep
    // installing methods in place and the base chain carries the mixin.
    auto* newCls = shape->newChild(ctx, /*isMutable=*/true);
    TransientPin pinNew(ctx, newCls);

    // Copy every OWN attribute of the old class onto the rebuilt class — its
    // methods, `__class_name__`, class-side markers. getOwnAttributes does NOT
    // walk the parent chain, so only the class's own behaviour is copied; the
    // inherited behaviour comes from the shape's parent chain.
    const proto::ProtoSparseList* own = oldCls->getOwnAttributes(ctx);
    if (own) {
        auto* it = const_cast<proto::ProtoSparseListIterator*>(
            own->getIterator(ctx));
        while (it && it->hasNext(ctx)) {
            unsigned long key = it->nextKey(ctx);
            const proto::ProtoObject* val = it->nextValue(ctx);
            // The own-attribute key is an interned-symbol ProtoString whose
            // pointer is stored as the sparse-list index.
            auto* sym = reinterpret_cast<const proto::ProtoObject*>(key)
                            ->asString(ctx);
            if (sym) newCls->setAttribute(ctx, sym, val);
            it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
        }
    }

    // Rebind the class's global name to the rebuilt object so subsequent
    // `ClassName` PUSH_GLOBALs and `ClassName >> sel` installs see it.
    const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");
    const proto::ProtoObject* nameObj =
        oldCls->getOwnAttributeDirect(ctx, nameKey);
    if (nameObj && nameObj != PROTO_NONE) {
        auto* nameStr = nameObj->asString(ctx);
        if (nameStr) {
            std::string name = nameStr->toStdString(ctx);
            auto* nameSym =
                proto::ProtoString::createSymbol(ctx, name.c_str());
            if (auto* g = rt.globals()) g->setAttribute(ctx, nameSym, newCls);
        }
    }
    return newCls;
}
} // namespace

// aClass addBehavior: aMixin   (alias: aClass addParent: aClass2)
//   → composes `aMixin`'s behaviour into `aClass` at runtime. Returns the
//     rebuilt class. The class object and every instance created AFTER the
//     call respond to the mixin's methods; pre-existing instances keep the
//     parent chain frozen at their construction (see docs/LANGUAGE.md §4.12).
const proto::ProtoObject* prim_Object_addBehavior(STRuntime& rt,
                                                  proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 1)
        throw std::runtime_error("addBehavior: expects 1 arg (a mixin class)");
    return addBehaviorToClass(rt, ctx, r, a[0]);
}

// superClass subclass: #Name uses: aCollection
//   → fresh named subclass of superClass that also inherits each class in the
//     `uses:` collection. The expression-receiver counterpart of the textual
//     `subclass:uses:` form, so an imported module class can be the primary
//     superclass: `(lib Counter) subclass: #Fast uses: { MixA }`.
const proto::ProtoObject* prim_Object_subclassUses(
    STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* r,
    const proto::ProtoObject* const* a, int argc) {
    if (argc != 2)
        throw std::runtime_error("subclass:uses: expects 2 args");
    return makeSubclassWithMixins(rt, ctx, r, a[0], a[1]);
}

// superClass subclass: #Name instanceVariableNames: 'a b' uses: aCollection
//   → as subclass:instanceVariableNames:, plus the `uses:` mixins.
const proto::ProtoObject* prim_Object_subclassIvarsUses(
    STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* r,
    const proto::ProtoObject* const* a, int argc) {
    if (argc != 3)
        throw std::runtime_error(
            "subclass:instanceVariableNames:uses: expects 3 args");
    if (a[1] && a[1] != PROTO_NONE && !a[1]->asString(ctx))
        throw std::runtime_error(
            "subclass:instanceVariableNames:uses:: ivar names must be a string");
    return makeSubclassWithMixins(rt, ctx, r, a[0], a[2]);
}

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
    const proto::ProtoString* nameKey =
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
    // interning and the getAttribute walk. Pin it.
    TransientPin pinWrapper(ctx, wrapper);
    // Unwrap: getImportModule returns a wrapper with `exports` attribute
    // pointing to the module.
    //
    // T5-a: resolve `exportsKey` FRESH from the live ctx every call — symbols
    // are interned per-ProtoSpace, so a function-local `static` would bind to
    // the FIRST runtime's space and never match the `exports` attribute key
    // that protoCore's getImportModule stamps on the wrapper in a LATER
    // runtime's space (the multi-runtime unit harness, and any host embedding
    // protoST alongside a foreign runtime). A stale key made the unwrap miss
    // and return the wrapper itself, so `m Widget` saw `doesNotUnderstand`.
    const proto::ProtoString* exportsKey =
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

// recv setInstVar: #name from: expected to: newValue
//
// Optimistic-concurrency compare-and-swap on a single instance variable. If
// `recv`'s instance variable `name` currently holds (by pointer identity)
// `expected`, atomically replace it with `newValue` and answer true;
// otherwise write nothing and answer false. This exposes protoCore's atomic
// attribute CAS (ProtoObject::setAttributeIfEqual) directly — the building
// block for lock-free, user-controlled retry:
//
//   [ old := obj count.
//     new := old + 1.
//     obj setInstVar: #count from: old to: new ] whileFalse.
//
// A failed CAS means another thread won the race; the loop re-reads the now
// current value and retries. Because the comparison is pointer identity over
// immutable snapshots, the classic ABA hazard does not arise: if the pointer
// is unchanged, the value genuinely is the one observed.
//
// Instance variables are stored under the mangled key "_iv_<name>" (see
// PUSH_INSTVAR / STORE_INSTVAR in the engine), so this never collides with a
// same-named method selector. An unset instance variable reads as nil; pass
// `nil` as `expected` to match it.
const proto::ProtoObject* prim_Object_setInstVarFromTo(
        STRuntime&, proto::ProtoContext* ctx,
        const proto::ProtoObject* r,
        const proto::ProtoObject* const* a, int argc) {
    if (argc != 3)
        throw std::runtime_error("setInstVar:from:to: expects 3 arguments");
    auto* nameStr = a[0] ? a[0]->asString(ctx) : nullptr;
    if (!nameStr)
        throw std::runtime_error(
            "setInstVar:from:to: — the variable name must be a symbol or string");
    std::string mangled = "_iv_";
    mangled += nameStr->toStdString(ctx);
    auto* slot = proto::ProtoString::createSymbol(ctx, mangled.c_str());
    const proto::ProtoObject* expected = a[1] ? a[1] : PROTO_NONE;
    const proto::ProtoObject* newValue = a[2] ? a[2] : PROTO_NONE;

    // An unset instance variable is physically absent yet reads as nil. Map a
    // `nil` expectation onto the absent slot so a CAS "from nil" matches it;
    // every other case passes `expected` straight through to the CAS, which
    // re-validates it atomically.
    const proto::ProtoObject* cur = r->getOwnAttributeDirect(ctx, slot);
    const proto::ProtoObject* casExpected =
        (cur == nullptr && expected == PROTO_NONE) ? nullptr : expected;
    bool ok = const_cast<proto::ProtoObject*>(r)
        ->setAttributeIfEqual(ctx, slot, casExpected, newValue);
    return ok ? PROTO_TRUE : PROTO_FALSE;
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
    // Optimistic-concurrency CAS on a single instance variable — the raw,
    // unwrapped form of protoCore's atomic attribute compare-and-swap. See
    // the Atom class for the wrapped idiom.
    bindPrimitive(rt, b.objectProto, "setInstVar:from:to:",
                  reg.registerPrim(prim_Object_setInstVarFromTo));
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
    // T3-b: multiple inheritance / mixins. `subclass:uses:` and
    // `subclass:instanceVariableNames:uses:` assemble a class with several
    // parents. The compiler's textual `Object subclass: #Foo uses: { … }`
    // form desugars to a send of these selectors; they also serve the
    // expression-receiver form (e.g. an imported module class).
    bindPrimitive(rt, b.objectProto, "subclass:uses:",
                  reg.registerPrim(prim_Object_subclassUses));
    bindPrimitive(rt, b.objectProto, "subclass:instanceVariableNames:uses:",
                  reg.registerPrim(prim_Object_subclassIvarsUses));
    // T3-c: on-the-fly behaviour composition. `addBehavior:` composes a mixin
    // into a class at runtime by rebuilding the class with the mixin baked
    // into its base chain; `addParent:` is a lower-level alias. Affects the
    // class and instances created after the call (see docs/LANGUAGE.md §4.12).
    {
        auto addBehaviorPrim = reg.registerPrim(prim_Object_addBehavior);
        bindPrimitive(rt, b.objectProto, "addBehavior:", addBehaviorPrim);
        bindPrimitive(rt, b.objectProto, "addParent:", addBehaviorPrim);
    }
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

// F6 v6 (2026-05-23 night): WorkerPool singleton — exposes the actor
// scheduler's pool-pause API to Smalltalk. Pattern matches Import:
// a mutable child of objectProto, primitives bound on it, registered
// in globals so PUSH_GLOBAL resolves `WorkerPool` directly.
//
// Used by `benchmarks/actors/saturation.st` to isolate pool drain
// capacity from main-thread producer rate: stopProcessing before
// loading mailboxes, startProcessing after, time the drain in
// between. The primitives are zero-arg, return the receiver.
namespace {
const proto::ProtoObject* prim_WorkerPool_stop(STRuntime& rt,
                                                proto::ProtoContext* /*ctx*/,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* /*a*/,
                                                int /*argc*/) {
    rt.stopProcessing();
    return r;
}
const proto::ProtoObject* prim_WorkerPool_start(STRuntime& rt,
                                                 proto::ProtoContext* /*ctx*/,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* /*a*/,
                                                 int /*argc*/) {
    rt.startProcessing();
    return r;
}
const proto::ProtoObject* prim_WorkerPool_paused(STRuntime& rt,
                                                  proto::ProtoContext* /*ctx*/,
                                                  const proto::ProtoObject* /*r*/,
                                                  const proto::ProtoObject* const* /*a*/,
                                                  int /*argc*/) {
    return rt.isProcessingPaused() ? PROTO_TRUE : PROTO_FALSE;
}
} // namespace

void installWorkerPoolGlobal(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    auto* ctx = rt.rootCtx();

    auto* pool = const_cast<proto::ProtoObject*>(b.objectProto)
        ->newChild(ctx, /*isMutable=*/true);

    bindPrimitive(rt, pool, "stopProcessing",
                  reg.registerPrim(prim_WorkerPool_stop));
    bindPrimitive(rt, pool, "startProcessing",
                  reg.registerPrim(prim_WorkerPool_start));
    bindPrimitive(rt, pool, "isPaused",
                  reg.registerPrim(prim_WorkerPool_paused));

    auto* key = proto::ProtoString::createSymbol(ctx, "WorkerPool");
    auto* g = rt.globals();
    g->setAttribute(ctx, key, pool);
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
