// Track 2, slice a (COL-a): the collection foundation.
//
// Implements the concrete `Array` base operations and the shared, derived
// iteration protocol bound on the abstract `Collection` prototype. The
// abstract prototypes (`Collection`, `SequenceableCollection`,
// `HashedCollection`) and `Array` itself are installed by Bootstrap; this
// file binds the primitives onto them.
//
// Representation — the mutable-holder pattern (from protoPython):
//
//   An `Array` instance is a mutable `ProtoObject` (a `newChild` of the
//   `Array` prototype) whose `__data__` attribute holds an immutable protoCore
//   `ProtoList` (wrapped as a `ProtoObject` via `asList`/`asObject`). protoCore
//   collections are immutable / structural-sharing; a mutation (`at:put:`)
//   replaces `__data__` with the new snapshot the protoCore mutator returns —
//   copy-on-write, O(log n), no per-element rewrite.
//
// Indexing — 1-based (Smalltalk convention). `at: 1` is the first element;
// the C++/protoCore `ProtoList` is 0-based, so every `at:` / `at:put:` /
// error message converts once at the boundary.
//
// Base operations vs. derived protocol:
//   * Base operations (`size`, `at:`, `at:put:`, `do:`, class-side `new:` /
//     `withAll:`) are C++ primitives, thin over the backing `ProtoList`,
//     bound on the `Array` prototype.
//   * The derived protocol (`collect:`, `select:`, `reject:`, `detect:`,
//     `detect:ifNone:`, `inject:into:`, `do:separatedBy:`, `count:`,
//     `anySatisfy:`, `allSatisfy:`, `,`, `asArray`, `isEmpty`, `notEmpty`,
//     `size`, `species`) is written once, bound on `Collection`, inherited by
//     every collection. It is built on the `forEachElement` helper plus the
//     user block. `forEachElement` dispatches on the receiver's collection
//     kind — for COL-a only `Array` is concrete; COL-b..e (OrderedCollection,
//     Set, Bag, Dictionary, Interval) extend the dispatch.
//
// Errors — `at:` out of range and `detect:` with no match throw a
// `std::runtime_error`. The engine wraps every primitive call in
// `translateNativeException`, which turns that into a catchable protoST
// `Error`, so `[ ... ] on: Error do: [ ... ]` guards it (Track 1).

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

// invokeBlock — defined in block_prims.cpp. Runs a BlockClosure synchronously
// in a nested ExecutionEngine with the supplied arguments.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc);

namespace {

// Attribute keys. Resolved fresh from the live ctx each call — protoCore
// interns symbols per-ProtoSpace, so a function-local static would bind to
// the first runtime's space and dangle for every later STRuntime.
const proto::ProtoString* dataKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__data__");
}

// The backing ProtoList of an Array instance. The instance stores the list
// (wrapped as a ProtoObject) under `__data__`; this unwraps it. A nullptr or
// missing `__data__` yields a fresh empty list — defensive, so a hand-built
// or partially-initialised instance never crashes.
const proto::ProtoList* arrayData(proto::ProtoContext* ctx,
                                  const proto::ProtoObject* arr) {
    const proto::ProtoObject* d =
        arr ? arr->getAttribute(ctx, dataKey(ctx)) : nullptr;
    if (!d || d == PROTO_NONE) return ctx->newList();
    const proto::ProtoList* l = d->asList(ctx);
    return l ? l : ctx->newList();
}

// The backing ProtoList of any list-backed collection instance — `Array` and
// `OrderedCollection` share the exact same `__data__` representation.
const proto::ProtoList* listData(proto::ProtoContext* ctx,
                                 const proto::ProtoObject* coll) {
    return arrayData(ctx, coll);
}

// True when `obj` is a list-backed collection instance — it carries `__data__`
// reachable through the prototype chain. Both `Array` and `OrderedCollection`
// answer true, so forEachElement's single ProtoList arm covers both.
bool isListBacked(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return false;
    const proto::ProtoObject* d = obj->getAttribute(ctx, dataKey(ctx));
    return d && d != PROTO_NONE;
}

// Replace a list-backed collection's `__data__` with a fresh snapshot. Every
// mutator (`add:`, `at:put:`, `removeFirst`, …) ends here. The new list is
// pinned across the key interning + setAttribute, both of which allocate.
void setData(proto::ProtoContext* ctx, const proto::ProtoObject* coll,
             const proto::ProtoList* updated) {
    TransientPin pinUpdated(
        ctx, reinterpret_cast<const proto::ProtoObject*>(updated));
    const_cast<proto::ProtoObject*>(coll)->setAttribute(
        ctx, dataKey(ctx), updated->asObject(ctx));
}

} // namespace

// --- makeArrayInstance — the canonical Array constructor -------------------
//
// Builds a fresh Array instance: a mutable child of the `Array` prototype with
// its `__data__` attribute set to `data` (wrapped as a ProtoObject). This is
// the single place an Array is materialised — the MAKE_ARRAY opcode, the
// class-side `new:` / `withAll:`, and every derived primitive that produces a
// new collection all route through here.
//
// Exposed (non-anonymous, declared in ExecutionEngine.cpp) so the MAKE_ARRAY
// opcode handler can call it.
const proto::ProtoObject* makeArrayInstance(STRuntime& rt,
                                            proto::ProtoContext* ctx,
                                            const proto::ProtoList* data) {
    if (!data) data = ctx->newList();
    // Pin the backing list across newChild + the key interning + setAttribute,
    // all of which allocate.
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    const proto::ProtoObject* arr =
        const_cast<proto::ProtoObject*>(rt.bootstrap().arrayProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinArr(ctx, arr);
    const_cast<proto::ProtoObject*>(arr)->setAttribute(
        ctx, dataKey(ctx), data->asObject(ctx));
    return arr;
}

// --- makeInstanceOfSpecies — wrap a ProtoList as an instance of any class ---
//
// `Array` and `OrderedCollection` share the mutable-holder representation: an
// instance is a mutable child of the class prototype with `__data__` = a
// ProtoList. This builds such an instance for ANY of those class prototypes —
// the single "wrap as species" helper the derived protocol uses to honour
// `species` (an OrderedCollection's collect: yields an OrderedCollection, an
// Array's an Array). For `Array` it is exactly makeArrayInstance.
const proto::ProtoObject* makeInstanceOfSpecies(STRuntime& /*rt*/,
                                                proto::ProtoContext* ctx,
                                                const proto::ProtoObject* classProto,
                                                const proto::ProtoList* data) {
    if (!data) data = ctx->newList();
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    const proto::ProtoObject* inst =
        const_cast<proto::ProtoObject*>(classProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinInst(ctx, inst);
    const_cast<proto::ProtoObject*>(inst)->setAttribute(
        ctx, dataKey(ctx), data->asObject(ctx));
    return inst;
}

namespace {

// --- speciesProtoOf — the class to build derived results from --------------
//
// `species` answers the receiver's own concrete class so a derived operation
// (collect:/select:/reject:/,) yields a collection of the same kind. We resolve
// it by walking the receiver's prototype chain: the first known concrete
// collection prototype it descends from IS its species. A receiver that is none
// of them (an abstract `Collection`, or some hand-built object) falls back to
// `Array` — the sensible default the spec mandates.
const proto::ProtoObject* speciesProtoOf(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r) {
    const proto::ProtoObject* ocProto    = rt.bootstrap().orderedCollectionProto;
    const proto::ProtoObject* arrayProto = rt.bootstrap().arrayProto;
    for (const proto::ProtoObject* p = r; p && p != PROTO_NONE;
         p = p->getPrototype(ctx)) {
        if (p == ocProto)    return ocProto;
        if (p == arrayProto) return arrayProto;
    }
    return arrayProto;
}

// --- forEachElement — the shared iteration core ----------------------------
//
// Iterates a collection's elements in order, invoking `fn` for each. The
// dispatch is on the receiver's collection KIND, not its exact class: the
// ProtoList arm covers every list-backed collection — `Array` (COL-a) and
// `OrderedCollection` (COL-b) both flow through it unchanged, because both
// store their elements in a `__data__` ProtoList. The structure (a kind-
// dispatch before iterating) is the extension point for COL-c..e — Set/Bag
// iterate a ProtoSet/ProtoMultiset, Interval computes elements lazily. Adding
// a kind there needs no change to any derived primitive: they all call
// forEachElement.
//
// `fn` returns false to stop early (used by detect:/anySatisfy:/allSatisfy:);
// forEachElement returns true if it ran to completion, false if `fn` stopped it.
template <typename Fn>
bool forEachElement(STRuntime& /*rt*/, proto::ProtoContext* ctx,
                    const proto::ProtoObject* collection, Fn&& fn) {
    if (isListBacked(ctx, collection)) {
        const proto::ProtoList* data = arrayData(ctx, collection);
        unsigned long n = data->getSize(ctx);
        for (unsigned long i = 0; i < n; ++i) {
            const proto::ProtoObject* e = data->getAt(ctx, static_cast<int>(i));
            if (!fn(e ? e : PROTO_NONE)) return false;
        }
        return true;
    }
    // COL-b..e: extend here (Set / Bag / Dictionary / Interval kinds).
    throw std::runtime_error("collection does not understand iteration");
}

// =====================  Array base operations  ============================

// anArray size → element count (a SmallInteger)
const proto::ProtoObject* prim_Array_size(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const*, int) {
    return ctx->fromLong(
        static_cast<long long>(arrayData(ctx, r)->getSize(ctx)));
}

// anArray isEmpty → true when size = 0
const proto::ProtoObject* prim_Array_isEmpty(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    return arrayData(ctx, r)->getSize(ctx) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

// anArray notEmpty → true when size > 0
const proto::ProtoObject* prim_Array_notEmpty(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    return arrayData(ctx, r)->getSize(ctx) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

// anArray at: index → the element at the 1-based index. Out of range signals
// an Error (via std::runtime_error → translateNativeException).
const proto::ProtoObject* prim_Array_at(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a,
                                        int argc) {
    if (argc != 1) throw std::runtime_error("at: expects 1 arg (index)");
    const proto::ProtoList* data = arrayData(ctx, r);
    long long idx1 = a[0]->asLong(ctx);              // 1-based
    long long n    = static_cast<long long>(data->getSize(ctx));
    if (idx1 < 1 || idx1 > n) {
        throw std::runtime_error(
            "Array>>at:: index " + std::to_string(idx1) +
            " out of range 1.." + std::to_string(n));
    }
    const proto::ProtoObject* e =
        data->getAt(ctx, static_cast<int>(idx1 - 1));  // → 0-based
    return e ? e : PROTO_NONE;
}

// anArray at: index put: value → replace the element at the 1-based index by
// swapping `__data__` with the snapshot setAt returns. Returns the stored
// value (Smalltalk at:put: convention). Out of range signals an Error.
const proto::ProtoObject* prim_Array_atPut(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a,
                                           int argc) {
    if (argc != 2) throw std::runtime_error("at:put: expects 2 args (index, value)");
    const proto::ProtoList* data = arrayData(ctx, r);
    long long idx1 = a[0]->asLong(ctx);              // 1-based
    long long n    = static_cast<long long>(data->getSize(ctx));
    if (idx1 < 1 || idx1 > n) {
        throw std::runtime_error(
            "Array>>at:put:: index " + std::to_string(idx1) +
            " out of range 1.." + std::to_string(n));
    }
    const proto::ProtoObject* value = a[1] ? a[1] : PROTO_NONE;
    const proto::ProtoList* updated =
        data->setAt(ctx, static_cast<int>(idx1 - 1), value);
    // `updated` is a fresh list held across the key interning + setAttribute.
    TransientPin pinUpdated(
        ctx, reinterpret_cast<const proto::ProtoObject*>(updated));
    const_cast<proto::ProtoObject*>(r)->setAttribute(
        ctx, dataKey(ctx), updated->asObject(ctx));
    return value;
}

// anArray do: aBlock → evaluate the one-arg block for each element, in order.
// Returns the receiver.
const proto::ProtoObject* prim_Array_do(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a,
                                        int argc) {
    if (argc != 1) throw std::runtime_error("do: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    TransientPin pinRecv(ctx, r);
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        invokeBlock(rt, ctx, block, &arg0, 1);
        return true;
    });
    return r;
}

// =====================  Array class-side constructors  =====================
//
// Class-side methods are bound on the `Array` prototype: a class object IS its
// prototype, so `getAttribute` from `Array` resolves these — exactly how
// `Counter class >> startingAt:` works in protoST today.

// Array new: n → an Array of `n` nil elements.
const proto::ProtoObject* prim_Array_classNew(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* /*cls*/,
                                              const proto::ProtoObject* const* a,
                                              int argc) {
    if (argc != 1) throw std::runtime_error("new: expects 1 arg (size)");
    long long n = a[0]->asLong(ctx);
    if (n < 0) throw std::runtime_error("Array new: negative size");
    const proto::ProtoList* data = ctx->newList();
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    for (long long i = 0; i < n; ++i) {
        data = data->appendLast(ctx, PROTO_NONE);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
    }
    return makeArrayInstance(rt, ctx, data);
}

// Array withAll: aCollection → an Array of the elements of `aCollection`. The
// argument may be any collection the iteration protocol understands (an Array
// for COL-a; OrderedCollection/Set/... once they land). This is the
// constructor the literal lowering could target; MAKE_ARRAY goes straight to
// makeArrayInstance, but `withAll:` is the script-visible equivalent.
const proto::ProtoObject* prim_Array_classWithAll(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* /*cls*/,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 1) throw std::runtime_error("withAll: expects 1 arg (collection)");
    const proto::ProtoList* data = ctx->newList();
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    forEachElement(rt, ctx, a[0], [&](const proto::ProtoObject* e) {
        data = data->appendLast(ctx, e);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
        return true;
    });
    return makeArrayInstance(rt, ctx, data);
}

// Array with: ... — fixed-arity convenience constructors (1..4 elements).
const proto::ProtoObject* prim_Array_classWith(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* /*cls*/,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    const proto::ProtoList* data = ctx->newList();
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    for (int i = 0; i < argc; ++i) {
        data = data->appendLast(ctx, a[i] ? a[i] : PROTO_NONE);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
    }
    return makeArrayInstance(rt, ctx, data);
}

// =================  OrderedCollection base operations  =====================
//
// An `OrderedCollection` is the growable sequenceable collection. It shares the
// `Array` mutable-holder representation exactly — a mutable child of the
// `OrderedCollection` prototype with `__data__` = an immutable `ProtoList` —
// but adds growth/shrink: `add:`, `addFirst:`, `removeFirst`, … Every mutator
// swaps `__data__` for the new snapshot a protoCore `ProtoList` mutator returns
// (`appendLast`, `appendFirst`, `removeAt`, …) — copy-on-write, O(log n).
//
// Because the backing store is a ProtoList, `OrderedCollection` flows through
// forEachElement's ProtoList arm with no change, so the whole derived protocol
// (`collect:`/`select:`/`detect:`/`inject:into:`/…) works on it for free.

// anOrderedCollection size → element count.
const proto::ProtoObject* prim_OC_size(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const*, int) {
    return ctx->fromLong(
        static_cast<long long>(listData(ctx, r)->getSize(ctx)));
}

const proto::ProtoObject* prim_OC_isEmpty(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const*, int) {
    return listData(ctx, r)->getSize(ctx) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_OC_notEmpty(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    return listData(ctx, r)->getSize(ctx) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

// anOrderedCollection at: index → element at the 1-based index. Out of range
// signals an Error.
const proto::ProtoObject* prim_OC_at(STRuntime&, proto::ProtoContext* ctx,
                                     const proto::ProtoObject* r,
                                     const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("at: expects 1 arg (index)");
    const proto::ProtoList* data = listData(ctx, r);
    long long idx1 = a[0]->asLong(ctx);
    long long n    = static_cast<long long>(data->getSize(ctx));
    if (idx1 < 1 || idx1 > n) {
        throw std::runtime_error(
            "OrderedCollection>>at:: index " + std::to_string(idx1) +
            " out of range 1.." + std::to_string(n));
    }
    const proto::ProtoObject* e = data->getAt(ctx, static_cast<int>(idx1 - 1));
    return e ? e : PROTO_NONE;
}

// anOrderedCollection at: index put: value → replace the 1-based slot. Returns
// the stored value. Out of range signals an Error.
const proto::ProtoObject* prim_OC_atPut(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a, int argc) {
    if (argc != 2) throw std::runtime_error("at:put: expects 2 args (index, value)");
    const proto::ProtoList* data = listData(ctx, r);
    long long idx1 = a[0]->asLong(ctx);
    long long n    = static_cast<long long>(data->getSize(ctx));
    if (idx1 < 1 || idx1 > n) {
        throw std::runtime_error(
            "OrderedCollection>>at:put:: index " + std::to_string(idx1) +
            " out of range 1.." + std::to_string(n));
    }
    const proto::ProtoObject* value = a[1] ? a[1] : PROTO_NONE;
    setData(ctx, r, data->setAt(ctx, static_cast<int>(idx1 - 1), value));
    return value;
}

// anOrderedCollection add: anObject / addLast: anObject → append at the end.
// Returns the added element (Smalltalk `add:` answers its argument).
const proto::ProtoObject* prim_OC_add(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("add: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    setData(ctx, r, listData(ctx, r)->appendLast(ctx, e));
    return e;
}

// anOrderedCollection addFirst: anObject → prepend. Returns the added element.
const proto::ProtoObject* prim_OC_addFirst(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("addFirst: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    setData(ctx, r, listData(ctx, r)->appendFirst(ctx, e));
    return e;
}

// anOrderedCollection addAll: aCollection → append every element of the
// argument (any collection the iteration protocol understands). Returns the
// argument (Smalltalk `addAll:` convention).
const proto::ProtoObject* prim_OC_addAll(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("addAll: expects 1 arg (collection)");
    const proto::ProtoList* data = listData(ctx, r);
    TransientPin pinData(ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    forEachElement(rt, ctx, a[0], [&](const proto::ProtoObject* e) {
        data = data->appendLast(ctx, e);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
        return true;
    });
    setData(ctx, r, data);
    return a[0] ? a[0] : PROTO_NONE;
}

// anOrderedCollection removeFirst → remove and return the first element. Empty
// receiver signals an Error.
const proto::ProtoObject* prim_OC_removeFirst(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    const proto::ProtoList* data = listData(ctx, r);
    if (data->getSize(ctx) == 0)
        throw std::runtime_error("OrderedCollection>>removeFirst: collection is empty");
    const proto::ProtoObject* e = data->getFirst(ctx);
    setData(ctx, r, data->removeFirst(ctx));
    return e ? e : PROTO_NONE;
}

// anOrderedCollection removeLast → remove and return the last element. Empty
// receiver signals an Error.
const proto::ProtoObject* prim_OC_removeLast(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    const proto::ProtoList* data = listData(ctx, r);
    if (data->getSize(ctx) == 0)
        throw std::runtime_error("OrderedCollection>>removeLast: collection is empty");
    const proto::ProtoObject* e = data->getLast(ctx);
    setData(ctx, r, data->removeLast(ctx));
    return e ? e : PROTO_NONE;
}

// Locate the 0-based index of the first element equal to `value`, using
// protoCore's object comparison (the same path `ProtoList::has` answers to).
// Returns -1 when no element matches.
int indexOfEqual(proto::ProtoContext* ctx, const proto::ProtoList* data,
                 const proto::ProtoObject* value) {
    if (!value) value = PROTO_NONE;
    unsigned long n = data->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* e = data->getAt(ctx, static_cast<int>(i));
        if (!e) e = PROTO_NONE;
        if (e == value) return static_cast<int>(i);
        if (e->compare(ctx, value) == 0) return static_cast<int>(i);
    }
    return -1;
}

// anOrderedCollection remove: anObject → remove the first element equal to the
// argument; return the argument. Not found signals an Error.
const proto::ProtoObject* prim_OC_remove(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("remove: expects 1 arg (element)");
    const proto::ProtoList* data = listData(ctx, r);
    const proto::ProtoObject* value = a[0] ? a[0] : PROTO_NONE;
    int idx = indexOfEqual(ctx, data, value);
    if (idx < 0)
        throw std::runtime_error("OrderedCollection>>remove:: element not found");
    setData(ctx, r, data->removeAt(ctx, idx));
    return value;
}

// anOrderedCollection remove: anObject ifAbsent: aBlock → as remove:, but on a
// miss evaluate `aBlock` (no args) and return its value instead of signalling.
const proto::ProtoObject* prim_OC_removeIfAbsent(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 2)
        throw std::runtime_error("remove:ifAbsent: expects 2 args (element, block)");
    const proto::ProtoList* data = listData(ctx, r);
    const proto::ProtoObject* value = a[0] ? a[0] : PROTO_NONE;
    int idx = indexOfEqual(ctx, data, value);
    if (idx < 0) {
        const proto::ProtoObject* fallback = invokeBlock(rt, ctx, a[1], nullptr, 0);
        return fallback ? fallback : PROTO_NONE;
    }
    setData(ctx, r, data->removeAt(ctx, idx));
    return value;
}

// anOrderedCollection first / last → convenience accessors. Empty → Error.
const proto::ProtoObject* prim_OC_first(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    const proto::ProtoList* data = listData(ctx, r);
    if (data->getSize(ctx) == 0)
        throw std::runtime_error("OrderedCollection>>first: collection is empty");
    const proto::ProtoObject* e = data->getFirst(ctx);
    return e ? e : PROTO_NONE;
}

const proto::ProtoObject* prim_OC_last(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const*, int) {
    const proto::ProtoList* data = listData(ctx, r);
    if (data->getSize(ctx) == 0)
        throw std::runtime_error("OrderedCollection>>last: collection is empty");
    const proto::ProtoObject* e = data->getLast(ctx);
    return e ? e : PROTO_NONE;
}

// anOrderedCollection do: aBlock → evaluate the one-arg block for each element,
// in order. Returns the receiver.
const proto::ProtoObject* prim_OC_do(STRuntime& rt, proto::ProtoContext* ctx,
                                     const proto::ProtoObject* r,
                                     const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("do: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    TransientPin pinRecv(ctx, r);
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        invokeBlock(rt, ctx, block, &arg0, 1);
        return true;
    });
    return r;
}

// =================  OrderedCollection class-side constructors  ==============

// OrderedCollection new → a fresh empty OrderedCollection.
const proto::ProtoObject* prim_OC_classNew(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* /*cls*/,
                                           const proto::ProtoObject* const*, int) {
    return makeInstanceOfSpecies(rt, ctx, rt.bootstrap().orderedCollectionProto,
                                 ctx->newList());
}

// OrderedCollection withAll: aCollection → an OrderedCollection of the elements
// of the argument (any collection the iteration protocol understands).
const proto::ProtoObject* prim_OC_classWithAll(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* /*cls*/,
                                               const proto::ProtoObject* const* a,
                                               int argc) {
    if (argc != 1) throw std::runtime_error("withAll: expects 1 arg (collection)");
    const proto::ProtoList* data = ctx->newList();
    TransientPin pinData(ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    forEachElement(rt, ctx, a[0], [&](const proto::ProtoObject* e) {
        data = data->appendLast(ctx, e);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
        return true;
    });
    return makeInstanceOfSpecies(rt, ctx, rt.bootstrap().orderedCollectionProto, data);
}

// =====================  Derived iteration protocol  ========================
//
// Bound on `Collection`, inherited by every collection. Each is built on
// forEachElement + the user block.

// aCollection species → the receiver's own concrete class — the class the
// derived protocol (collect:/select:/reject:/,) builds its results from. As of
// COL-b there are two concrete sequenceable classes, so `species` is per-class:
// `Array>>species` → `Array`, `OrderedCollection>>species` → `OrderedCollection`.
// An abstract `Collection` receiver (or any object that is neither) defaults to
// `Array`. Each derived result-builder accumulates into a ProtoList and wraps it
// as an instance of this class via makeInstanceOfSpecies.
const proto::ProtoObject* prim_Collection_species(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const*, int) {
    return speciesProtoOf(rt, ctx, r);
}

// aCollection size → generic element count via forEachElement. `Array` has its
// own faster direct primitive; this serves any future collection without one.
const proto::ProtoObject* prim_Collection_size(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const*, int) {
    long long count = 0;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject*) {
        ++count;
        return true;
    });
    return ctx->fromLong(count);
}

const proto::ProtoObject* prim_Collection_isEmpty(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const*, int) {
    bool empty = true;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject*) {
        empty = false;
        return false;  // stop at the first element
    });
    return empty ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_Collection_notEmpty(STRuntime& rt, proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* r,
                                                   const proto::ProtoObject* const*, int) {
    bool empty = true;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject*) {
        empty = false;
        return false;
    });
    return empty ? PROTO_FALSE : PROTO_TRUE;
}

// aCollection collect: aBlock → a new collection of `aBlock value: each`. The
// result is of the receiver's `species` — an OrderedCollection collect: yields
// an OrderedCollection, an Array's an Array.
const proto::ProtoObject* prim_Collection_collect(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 1) throw std::runtime_error("collect: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    const proto::ProtoList* out = ctx->newList();
    TransientPin pinOut(ctx, reinterpret_cast<const proto::ProtoObject*>(out));
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        const proto::ProtoObject* mapped = invokeBlock(rt, ctx, block, &arg0, 1);
        out = out->appendLast(ctx, mapped ? mapped : PROTO_NONE);
        pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        return true;
    });
    return makeInstanceOfSpecies(rt, ctx, speciesProtoOf(rt, ctx, r), out);
}

// aCollection select: aBlock → a new collection (of the receiver's `species`)
// of the elements for which the block answers true. `reject:` keeps the
// elements for which it answers false.
const proto::ProtoObject* selectReject(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc,
                                       bool keepWhenTrue, const char* sel) {
    if (argc != 1) throw std::runtime_error(std::string(sel) + " expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    const proto::ProtoList* out = ctx->newList();
    TransientPin pinOut(ctx, reinterpret_cast<const proto::ProtoObject*>(out));
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        const proto::ProtoObject* keep = invokeBlock(rt, ctx, block, &arg0, 1);
        bool isTrue = (keep == PROTO_TRUE);
        if (isTrue == keepWhenTrue) {
            out = out->appendLast(ctx, e);
            pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        }
        return true;
    });
    return makeInstanceOfSpecies(rt, ctx, speciesProtoOf(rt, ctx, r), out);
}

const proto::ProtoObject* prim_Collection_select(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    return selectReject(rt, ctx, r, a, argc, /*keepWhenTrue=*/true, "select:");
}

const proto::ProtoObject* prim_Collection_reject(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    return selectReject(rt, ctx, r, a, argc, /*keepWhenTrue=*/false, "reject:");
}

// aCollection detect: aBlock → the first element the block answers true for.
// No match signals an Error (catchable by `on: Error do:`).
const proto::ProtoObject* prim_Collection_detect(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 1) throw std::runtime_error("detect: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    const proto::ProtoObject* found = nullptr;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        if (invokeBlock(rt, ctx, block, &arg0, 1) == PROTO_TRUE) {
            found = e;
            return false;  // stop
        }
        return true;
    });
    if (!found)
        throw std::runtime_error("detect: no element satisfies the block");
    return found;
}

// aCollection detect: aBlock ifNone: noneBlock → the first match, or the value
// of `noneBlock` when nothing matches.
const proto::ProtoObject* prim_Collection_detectIfNone(STRuntime& rt, proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* r,
                                                       const proto::ProtoObject* const* a,
                                                       int argc) {
    if (argc != 2)
        throw std::runtime_error("detect:ifNone: expects 2 args (block, none-block)");
    const proto::ProtoObject* block     = a[0];
    const proto::ProtoObject* noneBlock = a[1];
    const proto::ProtoObject* found = nullptr;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        if (invokeBlock(rt, ctx, block, &arg0, 1) == PROTO_TRUE) {
            found = e;
            return false;
        }
        return true;
    });
    if (found) return found;
    const proto::ProtoObject* fallback = invokeBlock(rt, ctx, noneBlock, nullptr, 0);
    return fallback ? fallback : PROTO_NONE;
}

// aCollection inject: start into: aBlock → fold. `acc` starts at `start`; each
// element updates it via `aBlock value: acc value: each`.
const proto::ProtoObject* prim_Collection_injectInto(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const* a,
                                                     int argc) {
    if (argc != 2)
        throw std::runtime_error("inject:into: expects 2 args (start, block)");
    const proto::ProtoObject* acc   = a[0] ? a[0] : PROTO_NONE;
    const proto::ProtoObject* block = a[1];
    TransientPin pinAcc(ctx, acc);
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* args2[2] = { acc, e };
        const proto::ProtoObject* next = invokeBlock(rt, ctx, block, args2, 2);
        acc = next ? next : PROTO_NONE;
        pinAcc.reset(acc);
        return true;
    });
    return acc;
}

// aCollection do: aBlock separatedBy: sepBlock → run `aBlock` for each element;
// run `sepBlock` (no args) between consecutive elements. Returns the receiver.
const proto::ProtoObject* prim_Collection_doSeparatedBy(STRuntime& rt, proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* r,
                                                        const proto::ProtoObject* const* a,
                                                        int argc) {
    if (argc != 2)
        throw std::runtime_error("do:separatedBy: expects 2 args (block, sep-block)");
    const proto::ProtoObject* block    = a[0];
    const proto::ProtoObject* sepBlock = a[1];
    TransientPin pinRecv(ctx, r);
    bool first = true;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        if (!first) invokeBlock(rt, ctx, sepBlock, nullptr, 0);
        first = false;
        const proto::ProtoObject* arg0 = e;
        invokeBlock(rt, ctx, block, &arg0, 1);
        return true;
    });
    return r;
}

// aCollection count: aBlock → how many elements the block answers true for.
const proto::ProtoObject* prim_Collection_count(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a,
                                                int argc) {
    if (argc != 1) throw std::runtime_error("count: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    long long n = 0;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        if (invokeBlock(rt, ctx, block, &arg0, 1) == PROTO_TRUE) ++n;
        return true;
    });
    return ctx->fromLong(n);
}

// aCollection anySatisfy: aBlock → true if the block answers true for any
// element. allSatisfy: → true if it answers true for every element.
const proto::ProtoObject* prim_Collection_anySatisfy(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const* a,
                                                     int argc) {
    if (argc != 1) throw std::runtime_error("anySatisfy: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    bool any = false;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        if (invokeBlock(rt, ctx, block, &arg0, 1) == PROTO_TRUE) {
            any = true;
            return false;
        }
        return true;
    });
    return any ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_Collection_allSatisfy(STRuntime& rt, proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* r,
                                                     const proto::ProtoObject* const* a,
                                                     int argc) {
    if (argc != 1) throw std::runtime_error("allSatisfy: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    bool all = true;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        const proto::ProtoObject* arg0 = e;
        if (invokeBlock(rt, ctx, block, &arg0, 1) != PROTO_TRUE) {
            all = false;
            return false;
        }
        return true;
    });
    return all ? PROTO_TRUE : PROTO_FALSE;
}

// aCollection , aCollection → a new collection (of the receiver's `species`)
// with the receiver's elements followed by the argument's.
const proto::ProtoObject* prim_Collection_concat(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 1) throw std::runtime_error(", expects 1 arg (collection)");
    const proto::ProtoList* out = ctx->newList();
    TransientPin pinOut(ctx, reinterpret_cast<const proto::ProtoObject*>(out));
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        out = out->appendLast(ctx, e);
        pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        return true;
    });
    forEachElement(rt, ctx, a[0], [&](const proto::ProtoObject* e) {
        out = out->appendLast(ctx, e);
        pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        return true;
    });
    return makeInstanceOfSpecies(rt, ctx, speciesProtoOf(rt, ctx, r), out);
}

// aCollection asArray → a new Array of the receiver's elements. For an Array
// receiver this is a copy; for any other collection it is the canonical
// conversion into the sequenceable world.
const proto::ProtoObject* prim_Collection_asArray(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const*, int) {
    const proto::ProtoList* out = ctx->newList();
    TransientPin pinOut(ctx, reinterpret_cast<const proto::ProtoObject*>(out));
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* e) {
        out = out->appendLast(ctx, e);
        pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        return true;
    });
    return makeArrayInstance(rt, ctx, out);
}

} // namespace

void installCollectionPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();

    // --- Array base operations — bound on the Array prototype --------------
    bindPrimitive(rt, b.arrayProto, "size",
                  reg.registerPrim(prim_Array_size));
    bindPrimitive(rt, b.arrayProto, "isEmpty",
                  reg.registerPrim(prim_Array_isEmpty));
    bindPrimitive(rt, b.arrayProto, "notEmpty",
                  reg.registerPrim(prim_Array_notEmpty));
    bindPrimitive(rt, b.arrayProto, "at:",
                  reg.registerPrim(prim_Array_at));
    bindPrimitive(rt, b.arrayProto, "at:put:",
                  reg.registerPrim(prim_Array_atPut));
    bindPrimitive(rt, b.arrayProto, "do:",
                  reg.registerPrim(prim_Array_do));

    // --- Array class-side constructors — bound on the Array prototype ------
    // A class object IS its prototype, so a send to `Array` resolves these.
    bindPrimitive(rt, b.arrayProto, "new:",
                  reg.registerPrim(prim_Array_classNew));
    bindPrimitive(rt, b.arrayProto, "withAll:",
                  reg.registerPrim(prim_Array_classWithAll));
    {
        int withIdx = reg.registerPrim(prim_Array_classWith);
        bindPrimitive(rt, b.arrayProto, "with:",                   withIdx);
        bindPrimitive(rt, b.arrayProto, "with:with:",              withIdx);
        bindPrimitive(rt, b.arrayProto, "with:with:with:",         withIdx);
        bindPrimitive(rt, b.arrayProto, "with:with:with:with:",    withIdx);
    }

    // --- OrderedCollection base operations — bound on the OC prototype -----
    bindPrimitive(rt, b.orderedCollectionProto, "size",
                  reg.registerPrim(prim_OC_size));
    bindPrimitive(rt, b.orderedCollectionProto, "isEmpty",
                  reg.registerPrim(prim_OC_isEmpty));
    bindPrimitive(rt, b.orderedCollectionProto, "notEmpty",
                  reg.registerPrim(prim_OC_notEmpty));
    bindPrimitive(rt, b.orderedCollectionProto, "at:",
                  reg.registerPrim(prim_OC_at));
    bindPrimitive(rt, b.orderedCollectionProto, "at:put:",
                  reg.registerPrim(prim_OC_atPut));
    bindPrimitive(rt, b.orderedCollectionProto, "do:",
                  reg.registerPrim(prim_OC_do));
    {
        int addIdx = reg.registerPrim(prim_OC_add);
        bindPrimitive(rt, b.orderedCollectionProto, "add:",     addIdx);
        bindPrimitive(rt, b.orderedCollectionProto, "addLast:", addIdx);
    }
    bindPrimitive(rt, b.orderedCollectionProto, "addFirst:",
                  reg.registerPrim(prim_OC_addFirst));
    bindPrimitive(rt, b.orderedCollectionProto, "addAll:",
                  reg.registerPrim(prim_OC_addAll));
    bindPrimitive(rt, b.orderedCollectionProto, "removeFirst",
                  reg.registerPrim(prim_OC_removeFirst));
    bindPrimitive(rt, b.orderedCollectionProto, "removeLast",
                  reg.registerPrim(prim_OC_removeLast));
    bindPrimitive(rt, b.orderedCollectionProto, "remove:",
                  reg.registerPrim(prim_OC_remove));
    bindPrimitive(rt, b.orderedCollectionProto, "remove:ifAbsent:",
                  reg.registerPrim(prim_OC_removeIfAbsent));
    bindPrimitive(rt, b.orderedCollectionProto, "first",
                  reg.registerPrim(prim_OC_first));
    bindPrimitive(rt, b.orderedCollectionProto, "last",
                  reg.registerPrim(prim_OC_last));

    // --- OrderedCollection class-side constructors -------------------------
    // A class object IS its prototype, so a send to `OrderedCollection`
    // resolves these. `new` / `withAll:` build a fresh empty / filled instance.
    bindPrimitive(rt, b.orderedCollectionProto, "new",
                  reg.registerPrim(prim_OC_classNew));
    bindPrimitive(rt, b.orderedCollectionProto, "withAll:",
                  reg.registerPrim(prim_OC_classWithAll));

    // --- Derived iteration protocol — bound on Collection, inherited by all -
    bindPrimitive(rt, b.collectionProto, "species",
                  reg.registerPrim(prim_Collection_species));
    bindPrimitive(rt, b.collectionProto, "size",
                  reg.registerPrim(prim_Collection_size));
    bindPrimitive(rt, b.collectionProto, "isEmpty",
                  reg.registerPrim(prim_Collection_isEmpty));
    bindPrimitive(rt, b.collectionProto, "notEmpty",
                  reg.registerPrim(prim_Collection_notEmpty));
    bindPrimitive(rt, b.collectionProto, "collect:",
                  reg.registerPrim(prim_Collection_collect));
    bindPrimitive(rt, b.collectionProto, "select:",
                  reg.registerPrim(prim_Collection_select));
    bindPrimitive(rt, b.collectionProto, "reject:",
                  reg.registerPrim(prim_Collection_reject));
    bindPrimitive(rt, b.collectionProto, "detect:",
                  reg.registerPrim(prim_Collection_detect));
    bindPrimitive(rt, b.collectionProto, "detect:ifNone:",
                  reg.registerPrim(prim_Collection_detectIfNone));
    bindPrimitive(rt, b.collectionProto, "inject:into:",
                  reg.registerPrim(prim_Collection_injectInto));
    bindPrimitive(rt, b.collectionProto, "do:separatedBy:",
                  reg.registerPrim(prim_Collection_doSeparatedBy));
    bindPrimitive(rt, b.collectionProto, "count:",
                  reg.registerPrim(prim_Collection_count));
    bindPrimitive(rt, b.collectionProto, "anySatisfy:",
                  reg.registerPrim(prim_Collection_anySatisfy));
    bindPrimitive(rt, b.collectionProto, "allSatisfy:",
                  reg.registerPrim(prim_Collection_allSatisfy));
    bindPrimitive(rt, b.collectionProto, ",",
                  reg.registerPrim(prim_Collection_concat));
    bindPrimitive(rt, b.collectionProto, "asArray",
                  reg.registerPrim(prim_Collection_asArray));
}

} // namespace protoST
