#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/TransientPin.h"
#include "runtime/ValueFormat.h"
#include "runtime/ExecutionEngine.h"
#include "protoCore.h"

#include <cctype>
#include <chrono>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <cmath>
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
//   __mailbox__ : a protoCore ProtoMPSCQueue — the lock-free, GC-traced
//                 multi-producer / single-consumer mailbox
//   __pending__ : the tail of a batch a turn took out of the queue but has not
//                 processed yet (absent or nil while there is none)
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

    // __wrapped__ = recv. The three keys below come from the Bootstrap symbol
    // cache rather than a fresh createSymbol: interning is a SymbolTable
    // hash + shard lock, and these are on the actor-creation path.
    actor->setAttribute(ctx, b.sym.wrapped, r);

    // __mailbox__ = a fresh ProtoMPSCQueue. Unlike the immutable ProtoList it
    // replaces, the queue object's identity never changes: a send pushes into
    // it in O(1) with no compare-and-swap retry on the attribute, and the
    // garbage collector traces the queued messages.
    const proto::ProtoMPSCQueue* queue = ctx->newMPSCQueue();
    // `queue` is held across asObject + setAttribute — pin it.
    TransientPin pinQueue(
        ctx, reinterpret_cast<const proto::ProtoObject*>(queue));
    actor->setAttribute(ctx, b.sym.mailbox, queue->asObject(ctx));

    // __state__ = 0 (idle)
    actor->setAttribute(ctx, b.sym.state, ctx->fromLong(0));

    // __sched__ = 0 — the lock-free scheduler's per-actor 3-state turn-
    // ownership flag (0 idle / 1 active / 2 active+wakeup-pending). It MUST
    // exist as SmallInteger 0 from birth so STRuntime::casSchedState's
    // 0->1 compare-and-swap has a concrete value to match against.
    actor->setAttribute(ctx, rt.bootstrap().sym.sched, ctx->fromLong(0));

    // No per-actor mutex and no scheduler mutex. A send is a ProtoMPSCQueue
    // push (lock-free, O(1), one cell) and the scheduler itself is lock-free
    // over ProtoObject::setAttributeIfEqual — protoCore's atomic attribute
    // CAS. ProtoMPSCQueue::takeAll admits a single consumer at a time, which
    // "at most one turn in flight per actor" guarantees: the __sched__ flag
    // stays non-zero for the whole turn — see STRuntime::drainOne /
    // STRuntime::schedule / STRuntime::finishDrain.

    return actor;
}

// Shared core for the three actor-creation primitives below. `band` is the
// scheduler priority (0=high, 1=medium, 2=low). asActor is the default
// medium path; asHighPriorityActor / asLowPriorityActor flip the
// `__priority__` attribute the scheduler reads at enqueueReady time.
//
// `actor` is returned by prim_Object_asActor with no live root from this
// function's stack — the next allocation (ctx->fromLong) can move/GC.
// Pin it across the SmallInt creation and the setAttribute.
static const proto::ProtoObject* makeActorWithPriority(
        STRuntime& rt, proto::ProtoContext* ctx,
        const proto::ProtoObject* r, long long band) {
    const proto::ProtoObject* actor = prim_Object_asActor(rt, ctx, r, nullptr, 0);
    if (!actor || actor == PROTO_NONE) return actor;
    TransientPin pinActor(ctx, actor);
    const proto::ProtoObject* prioVal = ctx->fromLong(band);
    TransientPin pinPrio(ctx, prioVal);
    const_cast<proto::ProtoObject*>(actor)
        ->setAttribute(ctx, rt.bootstrap().sym.priority, prioVal);
    return actor;
}

// Object>>asHighPriorityActor → an Actor wrapping recv that lands in the
// scheduler's High band. Drains before any Medium or Low actor; otherwise
// identical to `asActor` (same single-method invariant, same lock-free
// mailbox, same Future returns).
const proto::ProtoObject* prim_Object_asHighPriorityActor(
        STRuntime& rt, proto::ProtoContext* ctx,
        const proto::ProtoObject* r,
        const proto::ProtoObject* const*, int) {
    return makeActorWithPriority(rt, ctx, r, /*band=High*/ 0);
}

// Object>>asLowPriorityActor → an Actor that drains AFTER any Medium or
// High actor. Use for background hygiene work (telemetry, log flushes)
// that should yield to the data-plane traffic.
const proto::ProtoObject* prim_Object_asLowPriorityActor(
        STRuntime& rt, proto::ProtoContext* ctx,
        const proto::ProtoObject* r,
        const proto::ProtoObject* const*, int) {
    return makeActorWithPriority(rt, ctx, r, /*band=Low*/ 2);
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

// class __initClassVars: namesString
//   → for each whitespace-separated name in namesString, install `_iv_<name>`
//     = nil on the receiver (the class object). Returns recv so the SEND
//     leaves the class on the stack for whatever the compiler does next
//     (STORE_GLOBAL, DUP, etc.).
//
// D19 (2026-06-13): emitted by the compiler's ClassDecl path when the class
// declares `classVariableNames: '...'`. Class variables share the `_iv_<name>`
// mangled attribute key with instance variables, so the existing PUSH_INSTVAR
// chain walk picks them up from any instance. Mutation from instance-side
// methods is rejected at compile time — see Compiler emitExpr / emitStatement
// NodeKind::Assignment handlers and docs/STATUS.md D19.
const proto::ProtoObject* prim_Object_initClassVars(STRuntime&,
                                                    proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* r,
                                                    const proto::ProtoObject* const* a,
                                                    int argc) {
    if (argc != 1) throw std::runtime_error("__initClassVars: expects 1 arg");
    auto* namesObj = a[0] ? a[0]->asString(ctx) : nullptr;
    if (!namesObj)
        throw std::runtime_error(
            "__initClassVars:: names must be a string");
    std::string names = namesObj->toStdString(ctx);
    auto* cls = const_cast<proto::ProtoObject*>(r);
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        std::string mangled = "_iv_" + cur;
        const proto::ProtoString* key =
            proto::ProtoString::createSymbol(ctx, mangled.c_str());
        cls->setAttribute(ctx, key, PROTO_NONE);
        cur.clear();
    };
    for (char ch : names) {
        if (std::isspace(static_cast<unsigned char>(ch))) flush();
        else cur += ch;
    }
    flush();
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
// A protoCore object captures its parent chain at construction: `newChild`
// copies the prototype's chain into the child, and the child never re-reads
// it. So a parent added to a class AFTER an instance exists is invisible to
// that instance. (Since protoCore 2.0.0 `newChild` copies the prototype's
// CURRENT chain, so instances created after such a mutation DO see it —
// but existing ones still do not.) A multiply-inheriting class therefore has
// its FULL parent chain in place before any instance is created.
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
// THE PROTOCORE CONSTRAINT (probed directly against protoCore 2.0.0):
// protoCore captures an object's parent chain into its BASE CELL at
// construction, and the object never re-reads it. `newChild` copies the
// prototype's chain at the moment of the call, so a later `addParent` or
// `setParents` on the (mutable) class is invisible to instances that ALREADY
// exist. Instances created AFTER the mutation do see it — protoCore 1.x
// hid it from those too, because `newChild` copied the prototype's
// birth-state chain rather than its current one; protoCore 2.0.0 corrected
// that (CHANGELOG "Upgrading from 1.2.0", item 4). (Method *attributes*
// installed directly on the class via `>>` ARE seen by existing instances —
// lookup reaches the class object and reads its current own-attributes — but
// new *parents* are not.)
//
// So no construction can give the mixin to instances that already exist, and
// `addBehavior:` does not try. It REBUILDS the class as a fresh object whose
// base cell carries the mixin:
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
// `addBehavior:` captured its parent chain at its own construction; it keeps
// that chain and does NOT gain the mixin. Lifting this would require protoCore
// to let a constructed object observe later parent mutations of its prototype,
// which it deliberately does not do (a captured chain is what makes lookup
// allocation-free and snapshot-stable) — out of scope here. `addBehavior:`
// therefore has "future instances" semantics: it affects the class object and
// instances created after the call.
//
// NOTE (protoCore 2.0.0): a plain `addParent` on the live mutable class would
// now also give "future instances" semantics, so the rebuild is no longer the
// only way to reach them. The rebuild is kept because it is what the shipped
// resolution order, the `super` lookup (exactly one chain entry carries
// `__class_name__`) and the global rebind are specified against; replacing it
// with an in-place `addParent` is a behaviour change, not a migration.

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

// --- reflection --------------------------------------------------------------

} // namespace (reopened below)

// Look up `selector` on `recv` and run it with `args`: a user method (a
// block-shaped wrapper carrying __bc_ptr__) runs in a nested engine with self
// bound, a primitive through the registry. Answers nullptr when the receiver
// does not understand the selector. Shared by perform: and the kernel's
// reflective primitives.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood) {
    if (understood) *understood = false;
    const proto::ProtoObject* method = recv ? recv->getAttribute(ctx, selector)
                                            : rt.bootstrap().nilProto->getAttribute(ctx, selector);
    if (!method || method == PROTO_NONE) return nullptr;
    const proto::ProtoObject* bcPtrObj = method->getAttribute(ctx, rt.bootstrap().sym.bcPtr);
    if (bcPtrObj && bcPtrObj != PROTO_NONE && bcPtrObj->isInteger(ctx)) {
        const BytecodeModule* sub =
            reinterpret_cast<const BytecodeModule*>(bcPtrObj->asLong(ctx));
        if (sub->argCount() != argc + 1)
            throw std::runtime_error("wrong number of arguments for "
                                     + selector->toStdString(ctx));
        std::vector<const proto::ProtoObject*> methodArgs;
        methodArgs.reserve(static_cast<size_t>(argc) + 1);
        methodArgs.push_back(recv ? recv : PROTO_NONE);
        for (int i = 0; i < argc; ++i) methodArgs.push_back(args[i]);
        const proto::ProtoObject* capDict = method->getAttribute(ctx, rt.bootstrap().sym.captured);
        if (capDict == PROTO_NONE) capDict = nullptr;
        if (understood) *understood = true;
        ExecutionEngine eng(rt);
        return eng.runWithArgs(ctx, *sub, recv ? recv : PROTO_NONE, methodArgs.data(),
                               argc + 1, capDict);
    }
    if (method->isInteger(ctx)) {
        const long long marker = method->asLong(ctx);
        if (marker & (1LL << 62)) {
            auto fn = rt.registry().at(static_cast<int>(marker & ((1LL << 62) - 1)));
            if (understood) *understood = true;
            return fn(rt, ctx, recv ? recv : PROTO_NONE, args, argc);
        }
    }
    return nullptr;
}

namespace {
namespace {
const proto::ProtoString* selectorArg(proto::ProtoContext* ctx, const proto::ProtoObject* sel) {
    const proto::ProtoString* s = sel ? sel->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error("a selector (Symbol) was expected");
    return proto::ProtoString::createSymbol(ctx, s->toStdString(ctx).c_str());
}
} // namespace

// recv perform: #selector withArguments: anArray
const proto::ProtoObject* prim_Object_performWithArguments(STRuntime& rt, proto::ProtoContext* ctx,
                                                           const proto::ProtoObject* r,
                                                           const proto::ProtoObject* const* a, int argc) {
    if (argc != 2) throw std::runtime_error("perform:withArguments: expects 2 args");
    const proto::ProtoString* sel = selectorArg(ctx, a[0]);
    std::vector<const proto::ProtoObject*> args;
    if (a[1] && a[1] != PROTO_NONE) {
        const proto::ProtoString* dataKey = proto::ProtoString::createSymbol(ctx, "__data__");
        const proto::ProtoObject* elems = a[1]->getAttribute(ctx, dataKey);
        const proto::ProtoList* list = elems && elems != PROTO_NONE ? elems->asList(ctx) : nullptr;
        if (!list) throw std::runtime_error("perform:withArguments: expects an Array of arguments");
        for (unsigned long i = 0; i < list->getSize(ctx); ++i)
            args.push_back(list->getAt(ctx, static_cast<int>(i)));
    }
    bool understood = false;
    const proto::ProtoObject* res = sendDynamic(rt, ctx, r, sel, args.data(),
                                                static_cast<int>(args.size()), &understood);
    if (!understood)
        throw std::runtime_error("doesNotUnderstand: " + sel->toStdString(ctx));
    return res ? res : PROTO_NONE;
}

// recv respondsTo: #selector → true when a method answers it.
const proto::ProtoObject* prim_Object_respondsTo(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("respondsTo: expects 1 arg");
    const proto::ProtoString* sel = selectorArg(ctx, a[0]);
    const proto::ProtoObject* holder = r && r != PROTO_NONE ? r : rt.bootstrap().nilProto;
    const proto::ProtoObject* m = holder->getAttribute(ctx, sel);
    if (!m || m == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* bc = m->getAttribute(ctx, rt.bootstrap().sym.bcPtr);
    if (bc && bc != PROTO_NONE) return PROTO_TRUE;
    if (m->isInteger(ctx) && (m->asLong(ctx) & (1LL << 62))) return PROTO_TRUE;
    return PROTO_FALSE;
}

// aClass superclass → the next class up the chain, nil for Object.
const proto::ProtoObject* prim_Object_superclass(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const*, int) {
    const proto::ProtoString* nameKey = rt.bootstrap().sym.className;
    if (!r || r == PROTO_NONE) return PROTO_NONE;
    for (const proto::ProtoObject* p = r->getFirstParent(ctx); p && p != PROTO_NONE;
         p = p->getFirstParent(ctx)) {
        const proto::ProtoObject* n = p->getOwnAttributeDirect(ctx, nameKey);
        if (n && n != PROTO_NONE) return p;
    }
    return PROTO_NONE;
}

// recv shallowCopy → a new instance of the same class holding the same
// instance-variable values. Values (numbers, strings, nil, booleans) answer
// themselves. Collections copy their element storage attribute, which is an
// immutable protoCore list, so the copy and the original evolve independently.
const proto::ProtoObject* prim_Object_shallowCopy(STRuntime&, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const*, int) {
    if (!r || r == PROTO_NONE || r == PROTO_TRUE || r == PROTO_FALSE) return r;
    if (r->isInteger(ctx) || r->isFloat(ctx) || r->asString(ctx)) return r;
    const proto::ProtoObject* parent = r->getFirstParent(ctx);
    if (!parent || parent == PROTO_NONE) return r;
    const proto::ProtoObject* copy = parent->newChild(ctx, /*isMutable=*/true);
    struct Sink { const proto::ProtoObject* copy; };
    Sink sink{copy};
    r->processOwnAttributes(ctx, &sink,
        [](proto::ProtoContext* c, void* self, const proto::ProtoString* key,
           const proto::ProtoObject* value) {
            auto* s = static_cast<Sink*>(self);
            s->copy->setAttribute(c, key, value);
        });
    return copy;
}

// recv identityHash → a SmallInteger fixed for the object's lifetime.
const proto::ProtoObject* prim_Object_identityHash(STRuntime&, proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* r,
                                                   const proto::ProtoObject* const*, int) {
    const auto bits = reinterpret_cast<std::uintptr_t>(r);
    return ctx->fromLong(static_cast<long long>((bits >> 4) & ((1ULL << 54) - 1)));
}

// recv hash → consistent with =: numbers by value (1 hash = 1.0 hash), strings
// and symbols by content, every other object by identity.
const proto::ProtoObject* prim_Object_hash(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a, int argc) {
    if (!r || r == PROTO_NONE) return ctx->fromLong(0);
    if (r == PROTO_TRUE) return ctx->fromLong(1);
    if (r == PROTO_FALSE) return ctx->fromLong(2);
    if (r->isFloat(ctx)) {
        const double d = r->asDouble(ctx);
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15)
            return ctx->fromLong(static_cast<long long>(d) & ((1LL << 54) - 1));
        return ctx->fromLong(static_cast<long long>(std::hash<double>{}(d) & ((1ULL << 54) - 1)));
    }
    if (r->isInteger(ctx)) {
        if (r->compare(ctx, ctx->fromLong(1LL << 53)) < 0 && r->compare(ctx, ctx->fromLong(-(1LL << 53))) > 0)
            return ctx->fromLong(r->asLong(ctx) & ((1LL << 54) - 1));
        const std::string digits = formatNumber(ctx, r);
        return ctx->fromLong(static_cast<long long>(std::hash<std::string>{}(digits) & ((1ULL << 54) - 1)));
    }
    if (const proto::ProtoString* s = r->asString(ctx))
        return ctx->fromLong(static_cast<long long>(
            std::hash<std::string>{}(s->toStdString(ctx)) & ((1ULL << 54) - 1)));
    return prim_Object_identityHash(rt, ctx, r, a, argc);
}

// Nesting depth of printOn: on this thread: a collection that contains itself
// would otherwise recurse without end. The kernel stops at a fixed depth.
namespace { thread_local int t_printDepth = 0; }

const proto::ProtoObject* prim_Object_printEnter(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject*,
                                                 const proto::ProtoObject* const*, int) {
    return ctx->fromLong(++t_printDepth);
}

const proto::ProtoObject* prim_Object_printExit(STRuntime&, proto::ProtoContext*,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const*, int) {
    if (t_printDepth > 0) --t_printDepth;
    return r;
}

// recv __stdout: aString — write the text to standard output as is (the
// kernel's printNl / displayNl / Transcript build on it).
const proto::ProtoObject* prim_Object_stdout(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("__stdout: expects 1 arg");
    const proto::ProtoString* s = a[0] ? a[0]->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error("__stdout: expects a String");
    const std::string text = s->toStdString(ctx);
    std::fwrite(text.data(), 1, text.size(), stdout);
    if (!text.empty() && text.back() == '\n') std::fflush(stdout);
    return r;
}

// recv class → the receiver's class.
//
// Classes are prototypes that carry their name as an OWN `__class_name__`;
// an instance's class is the nearest such prototype on its chain. Tagged and
// primitive values map to their bootstrap prototypes (protoCore routes each
// kind to one). A class answers its metaclass: one object per class, created
// on first use and kept on the class, that prints as "<Name> class" and
// answers the class as `soleInstance`.
namespace {
constexpr long long kSmallIntegerMax = (1LL << 55) - 1;   // 56-bit tagged range
constexpr long long kSmallIntegerMin = -(1LL << 55);

const proto::ProtoObject* metaclassOf(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* cls,
                                      const proto::ProtoString* nameKey,
                                      const proto::ProtoObject* className) {
    // Symbols are interned per ProtoSpace: never cache them in a static.
    const proto::ProtoString* metaKey = proto::ProtoString::createSymbol(ctx, "__metaclass__");
    const proto::ProtoString* soleKey = proto::ProtoString::createSymbol(ctx, "__sole_instance__");
    const proto::ProtoObject* meta = cls->getOwnAttributeDirect(ctx, metaKey);
    if (meta && meta != PROTO_NONE) return meta;
    std::string name = "Class";
    if (const proto::ProtoString* ns = className->asString(ctx)) name = ns->toStdString(ctx);
    meta = rt.bootstrap().objectProto->newChild(ctx, /*isMutable=*/true);
    const_cast<proto::ProtoObject*>(meta)->setAttribute(
        ctx, nameKey, ctx->fromUTF8String((name + " class").c_str()));
    const_cast<proto::ProtoObject*>(meta)->setAttribute(ctx, soleKey, cls);
    const_cast<proto::ProtoObject*>(cls)->setAttribute(ctx, metaKey, meta);
    return meta;
}
} // namespace

const proto::ProtoObject* prim_Object_class(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    const auto& b = rt.bootstrap();
    if (!r || r == PROTO_NONE) return b.nilProto;
    if (r == PROTO_TRUE || r == PROTO_FALSE) return b.booleanProto;
    if (r->isFloat(ctx)) return b.floatProto;
    if (r->isInteger(ctx)) {
        const bool small = r->compare(ctx, ctx->fromLong(kSmallIntegerMax)) <= 0
                        && r->compare(ctx, ctx->fromLong(kSmallIntegerMin)) >= 0;
        return small ? b.smallIntegerProto : b.largeIntegerProto;
    }
    if (!r->isInteger(ctx) && r->getPrototype(ctx) == b.characterProto) return b.characterProto;
    if (r->asString(ctx)) {
        // The symbol tag lives on the value itself; asString may answer a
        // different handle, so the tag is read from the receiver.
        return reinterpret_cast<const proto::ProtoString*>(r)->isSymbol()
            ? b.symbolProto : b.stringProto;
    }
    const proto::ProtoString* nameKey = b.sym.className;
    const proto::ProtoObject* own = r->getOwnAttributeDirect(ctx, nameKey);
    if (own && own != PROTO_NONE) return metaclassOf(rt, ctx, r, nameKey, own);
    for (const proto::ProtoObject* p = r->getFirstParent(ctx); p && p != PROTO_NONE;
         p = p->getFirstParent(ctx)) {
        const proto::ProtoObject* name = p->getOwnAttributeDirect(ctx, nameKey);
        if (name && name != PROTO_NONE) return p;
    }
    return b.objectProto;
}

// aClass name → its name as a String ("Counter", "Counter class").
const proto::ProtoObject* prim_Object_name(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    const proto::ProtoObject* own =
        r ? r->getOwnAttributeDirect(ctx, rt.bootstrap().sym.className) : nullptr;
    if (!own || own == PROTO_NONE)
        throw std::runtime_error("doesNotUnderstand: name (the receiver is not a class)");
    return own;
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
        // basicNew: the raw allocator, kept available when a class overrides new.
        bindPrimitive(rt, b.objectProto, "basicNew", newChildPrim);
    }
    bindPrimitive(rt, b.objectProto, "__installMethod:as:",
                  reg.registerPrim(prim_Object_installMethod));
    bindPrimitive(rt, b.objectProto, "__installClassMethod:as:",
                  reg.registerPrim(prim_Object_installClassMethod));
    bindPrimitive(rt, b.objectProto, "asActor",
                  reg.registerPrim(prim_Object_asActor));
    bindPrimitive(rt, b.objectProto, "asHighPriorityActor",
                  reg.registerPrim(prim_Object_asHighPriorityActor));
    bindPrimitive(rt, b.objectProto, "asLowPriorityActor",
                  reg.registerPrim(prim_Object_asLowPriorityActor));
    // Optimistic-concurrency CAS on a single instance variable — the raw,
    // unwrapped form of protoCore's atomic attribute compare-and-swap. See
    // the Atom class for the wrapped idiom.
    bindPrimitive(rt, b.objectProto, "setInstVar:from:to:",
                  reg.registerPrim(prim_Object_setInstVarFromTo));
    // BL-3: rich printString. `__setClassName:` is emitted by the compiler's
    // ClassDecl path; `printString` is the default inherited by every object.
    bindPrimitive(rt, b.objectProto, "__setClassName:",
                  reg.registerPrim(prim_Object_setClassName));
    // D19: `__initClassVars:` is emitted by the compiler's ClassDecl path
    // when the class declares `classVariableNames: '...'`. It installs each
    // name as `_iv_<name>` = nil on the class object so instance-side reads
    // via the shared `_iv_<name>` chain walk pick up the shared storage.
    bindPrimitive(rt, b.objectProto, "__initClassVars:",
                  reg.registerPrim(prim_Object_initClassVars));
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
    bindPrimitive(rt, b.objectProto, "basicPrintString",
                  reg.registerPrim(prim_Object_printString));
    bindPrimitive(rt, b.objectProto, "__printEnter", reg.registerPrim(prim_Object_printEnter));
    bindPrimitive(rt, b.objectProto, "__printExit", reg.registerPrim(prim_Object_printExit));
    bindPrimitive(rt, b.objectProto, "__stdout:", reg.registerPrim(prim_Object_stdout));
    bindPrimitive(rt, b.objectProto, "class", reg.registerPrim(prim_Object_class));
    bindPrimitive(rt, b.objectProto, "name", reg.registerPrim(prim_Object_name));
    bindPrimitive(rt, b.objectProto, "perform:withArguments:",
                  reg.registerPrim(prim_Object_performWithArguments));
    bindPrimitive(rt, b.objectProto, "respondsTo:", reg.registerPrim(prim_Object_respondsTo));
    bindPrimitive(rt, b.objectProto, "superclass", reg.registerPrim(prim_Object_superclass));
    bindPrimitive(rt, b.objectProto, "shallowCopy", reg.registerPrim(prim_Object_shallowCopy));
    bindPrimitive(rt, b.objectProto, "identityHash", reg.registerPrim(prim_Object_identityHash));
    bindPrimitive(rt, b.objectProto, "hash", reg.registerPrim(prim_Object_hash));
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
