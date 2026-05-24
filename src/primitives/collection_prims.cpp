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

// The backing ProtoSet of a Set instance. Stored (wrapped as a ProtoObject)
// under `__data__`; this unwraps it. A nullptr or missing `__data__` yields a
// fresh empty set — defensive, like arrayData.
const proto::ProtoSet* setDataOf(proto::ProtoContext* ctx,
                                 const proto::ProtoObject* s) {
    const proto::ProtoObject* d =
        s ? s->getAttribute(ctx, dataKey(ctx)) : nullptr;
    if (!d || d == PROTO_NONE) return ctx->newSet();
    const proto::ProtoSet* set = d->asSet(ctx);
    return set ? set : ctx->newSet();
}

// True when `obj`'s `__data__` is a ProtoSet (i.e. a `Set` instance).
bool isSetBacked(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return false;
    const proto::ProtoObject* d = obj->getAttribute(ctx, dataKey(ctx));
    return d && d != PROTO_NONE && d->asSet(ctx) != nullptr;
}

// True when `obj` is a list-backed collection instance — it carries `__data__`
// reachable through the prototype chain holding a ProtoList. `Array`,
// `OrderedCollection` and `Bag` all answer true, so forEachElement's single
// ProtoList arm covers them. A `Set` instance also carries `__data__`, but
// holding a ProtoSet — so this checks the backing IS a ProtoList.
//
// `Bag` is intentionally ProtoList-backed (one slot per occurrence) rather
// than ProtoMultiset-backed: protoCore's ProtoMultiset stores element->count
// keyed by the element HASH and never retains the element object itself, so a
// ProtoMultiset cannot be iterated to recover its elements. A ProtoList of
// occurrences gives correct `do:` / `occurrencesOf:` / derived-protocol
// behaviour. `species` still resolves `Bag` by prototype identity, so a Bag's
// `collect:`/`select:` still yields a Bag.
bool isListBacked(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return false;
    const proto::ProtoObject* d = obj->getAttribute(ctx, dataKey(ctx));
    return d && d != PROTO_NONE && d->asList(ctx) != nullptr;
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

// Replace a Set instance's `__data__` with a fresh ProtoSet snapshot. Every
// Set mutator (`add:`, `remove:`) ends here.
void setSetData(proto::ProtoContext* ctx, const proto::ProtoObject* coll,
                const proto::ProtoSet* updated) {
    TransientPin pinUpdated(
        ctx, reinterpret_cast<const proto::ProtoObject*>(updated));
    const_cast<proto::ProtoObject*>(coll)->setAttribute(
        ctx, dataKey(ctx), updated->asObject(ctx));
}

// --- Dictionary backing — a hash->bucket ProtoSparseList -------------------
//
// protoCore's `ProtoSparseList` is keyed by `unsigned long`, so it cannot
// store arbitrary object keys directly. A `Dictionary` keeps its `__data__`
// as a `ProtoSparseList` keyed by `dictKeyHash(ctx, key)`; each slot holds a
// *bucket* — a `ProtoList` of alternating `[key0, value0, key1, value1, …]`.
// Multiple keys that collide on a hash coexist in one bucket, and the key
// objects themselves are retained (needed for `keysDo:` / `keys` / equality
// comparison). Lookup walks: hash → bucket → linear scan comparing keys by
// protoCore object equality (`compare(...) == 0`), exactly as Set/Bag's
// `remove:` does.
//
// **String key canonicalisation (2026-05-24).** protoCore's `ProtoString`
// hash is structural for rope-built strings: a `'memb' , 'ers'` rope and a
// `'members'` leaf are byte-equal AND `=` answers true, but their hashes
// differ because `StringInternalNode::subtreeHash` combines child hashes
// rather than folding bytes. A naive `key->getHash(ctx)` would therefore
// route the two through different ProtoSparseList slots and break lookup.
//
// `dictKeyHash` works around this by canonicalising any non-symbol
// ProtoString key through `ProtoString::createSymbol` first. The symbol
// table is content-keyed (doc: "Symbols with the same content share a
// unique pointer identity"), so two strings with identical bytes —
// regardless of internal rope structure — collapse to the SAME canonical
// symbol. The hash is then the symbol's pointer identity, which is stable
// across runs of the same process. Non-string keys (Integer, Boolean,
// arbitrary objects) fall through to `key->getHash(ctx)` unchanged.

// Canonical pointer hash for any object usable as a Dictionary key. For a
// non-symbol ProtoString it routes through the symbol table; for everything
// else it defers to the object's own hash protocol. The returned hash is
// stable for the lifetime of the process for any given content (because
// canonical symbols are perpetual).
unsigned long dictKeyHash(proto::ProtoContext* ctx,
                          const proto::ProtoObject* key) {
    if (!key || key == PROTO_NONE) return 0;
    if (key->isString(ctx)) {
        const proto::ProtoString* s = key->asString(ctx);
        if (s && !s->isSymbol()) {
            // Materialise once and intern. Rope strings cost an O(N) byte walk
            // on this path, but subsequent lookups of an equal-content key hit
            // the symbol-table cache in O(1) pointer-compare.
            std::string utf8 = s->toStdString(ctx);
            const proto::ProtoString* sym =
                proto::ProtoString::createSymbol(ctx, utf8);
            return reinterpret_cast<unsigned long>(sym);
        }
        if (s && s->isSymbol()) {
            // Already canonical — use its pointer identity directly.
            return reinterpret_cast<unsigned long>(s);
        }
    }
    return key->getHash(ctx);
}

// The backing ProtoSparseList of a Dictionary instance. A nullptr or missing
// `__data__` yields a fresh empty sparse list — defensive, like arrayData.
const proto::ProtoSparseList* dictData(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* d) {
    const proto::ProtoObject* raw =
        d ? d->getAttribute(ctx, dataKey(ctx)) : nullptr;
    if (!raw || raw == PROTO_NONE) return ctx->newSparseList();
    const proto::ProtoSparseList* sl = raw->asSparseList(ctx);
    return sl ? sl : ctx->newSparseList();
}

// True when `obj`'s `__data__` is a ProtoSparseList (i.e. a `Dictionary`).
bool isDictBacked(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return false;
    const proto::ProtoObject* raw = obj->getAttribute(ctx, dataKey(ctx));
    return raw && raw != PROTO_NONE && raw->asSparseList(ctx) != nullptr;
}

// Replace a Dictionary's `__data__` with a fresh ProtoSparseList snapshot.
// Every Dictionary mutator (`at:put:`, `removeKey:`) ends here.
void setDictData(proto::ProtoContext* ctx, const proto::ProtoObject* coll,
                 const proto::ProtoSparseList* updated) {
    TransientPin pinUpdated(
        ctx, reinterpret_cast<const proto::ProtoObject*>(updated));
    const_cast<proto::ProtoObject*>(coll)->setAttribute(
        ctx, dataKey(ctx), updated->asObject(ctx));
}

// --- Interval backing — a LAZY collection (no `__data__`) ------------------
//
// Track 2 slice e (COL-e): an `Interval` (`1 to: 10 [by: 2]`) does NOT store
// its elements. An instance is a mutable child of `intervalProto` carrying
// three integer attributes — `start`, `stop`, `step` — and computes each
// element on demand. There is no `__data__`; iteration strides from `start`
// by `step` while not past `stop`.

const proto::ProtoString* intervalStartKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__interval_start__");
}
const proto::ProtoString* intervalStopKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__interval_stop__");
}
const proto::ProtoString* intervalStepKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__interval_step__");
}

// Read one integer Interval bound; a missing/nil attribute defaults to `dflt`.
long long intervalAttr(proto::ProtoContext* ctx, const proto::ProtoObject* iv,
                        const proto::ProtoString* key, long long dflt) {
    const proto::ProtoObject* v = iv ? iv->getAttribute(ctx, key) : nullptr;
    if (!v || v == PROTO_NONE) return dflt;
    return v->asLong(ctx);
}

// The element count of an Interval. For `step > 0`: max(0, (stop-start)/step+1);
// the analogous count for `step < 0`. Integer arithmetic, never negative.
long long intervalSize(long long start, long long stop, long long step) {
    if (step == 0) return 0;                       // degenerate — empty
    if (step > 0) {
        if (stop < start) return 0;
        return (stop - start) / step + 1;
    }
    if (stop > start) return 0;
    return (start - stop) / (-step) + 1;
}

// True when `obj` descends from `intervalProto` — an `Interval` instance. An
// Interval has no `__data__`, so forEachElement detects it by prototype
// identity rather than by a backing-store probe.
//
// The walk is bounded: it stops at `objectProto`, the root of the protoST
// class tree. Walking past it into the protoCore built-in prototypes is both
// pointless (no Interval lives there) and unsafe — the protoCore root's
// `getPrototype` is self-referential, so an unbounded walk that never matches
// would spin forever. `speciesProtoOf` escapes that only because it always
// finds a concrete collection prototype before reaching the cycle.
bool isIntervalBacked(STRuntime& rt, proto::ProtoContext* ctx,
                      const proto::ProtoObject* obj) {
    const proto::ProtoObject* ivProto  = rt.bootstrap().intervalProto;
    const proto::ProtoObject* objProto = rt.bootstrap().objectProto;
    const proto::ProtoObject* prev     = nullptr;
    for (const proto::ProtoObject* p = obj; p && p != PROTO_NONE && p != prev;
         prev = p, p = p->getPrototype(ctx)) {
        if (p == ivProto) return true;
        if (p == objProto) break;   // reached the protoST root — no Interval above
    }
    return false;
}

} // namespace

// --- makeIntervalInstance — the canonical Interval constructor -------------
//
// Builds a fresh lazy Interval: a mutable child of `intervalProto` carrying
// the three integer bounds. No `__data__` — elements are computed on demand.
// Exposed (non-anonymous) so `Number>>to:` / `to:by:` can call it.
const proto::ProtoObject* makeIntervalInstance(STRuntime& rt,
                                               proto::ProtoContext* ctx,
                                               long long start,
                                               long long stop,
                                               long long step) {
    const proto::ProtoObject* iv =
        const_cast<proto::ProtoObject*>(rt.bootstrap().intervalProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinIv(ctx, iv);
    const_cast<proto::ProtoObject*>(iv)->setAttribute(
        ctx, intervalStartKey(ctx), ctx->fromLong(start));
    const_cast<proto::ProtoObject*>(iv)->setAttribute(
        ctx, intervalStopKey(ctx), ctx->fromLong(stop));
    const_cast<proto::ProtoObject*>(iv)->setAttribute(
        ctx, intervalStepKey(ctx), ctx->fromLong(step));
    return iv;
}

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

// --- makeInstanceOfSpecies — wrap accumulated elements as the target class --
//
// The derived protocol (`collect:`/`select:`/`reject:`/`,`) accumulates result
// elements into a `ProtoList`, then calls this once at the end of iteration to
// materialise an instance of the receiver's `species`. List-backed species
// (`Array`, `OrderedCollection`, `Bag`) wrap the `ProtoList` directly — for
// `Array` that is exactly makeArrayInstance, and a `Bag` stores one slot per
// occurrence so the list IS its backing. A `Set` species converts: it builds a
// fresh `ProtoSet` by `add`-ing each accumulated element (deduplicating). One
// end-of-collection conversion — the derived primitives never change how they
// accumulate.
const proto::ProtoObject* makeInstanceOfSpecies(STRuntime& rt,
                                                proto::ProtoContext* ctx,
                                                const proto::ProtoObject* classProto,
                                                const proto::ProtoList* data) {
    if (!data) data = ctx->newList();
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));

    // Set species: convert the accumulated ProtoList into a deduplicating
    // ProtoSet, then store that under `__data__`.
    if (classProto == rt.bootstrap().setProto) {
        const proto::ProtoObject* inst =
            const_cast<proto::ProtoObject*>(classProto)
                ->newChild(ctx, /*isMutable=*/true);
        TransientPin pinInst(ctx, inst);
        unsigned long n = data->getSize(ctx);
        const proto::ProtoSet* set = ctx->newSet();
        TransientPin pinSet(
            ctx, reinterpret_cast<const proto::ProtoObject*>(set));
        for (unsigned long i = 0; i < n; ++i) {
            const proto::ProtoObject* e =
                data->getAt(ctx, static_cast<int>(i));
            set = set->add(ctx, e ? e : PROTO_NONE);
            pinSet.reset(reinterpret_cast<const proto::ProtoObject*>(set));
        }
        const_cast<proto::ProtoObject*>(inst)->setAttribute(
            ctx, dataKey(ctx), set->asObject(ctx));
        return inst;
    }

    // List-backed species (`Array`, `OrderedCollection`, `Bag`): wrap directly.
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
//
// COL-e: an `Interval` is lazy and read-only — a `collect:`/`select:` result
// cannot be an Interval. An Interval matches none of the concrete prototypes
// below, so the walk falls through to the `arrayProto` default — exactly the
// spec's rule that `(1 to: 5) collect: [...]` yields an `Array`. The walk is
// bounded at `objectProto` (the protoST root): without that bound an Interval
// receiver — matching nothing — would walk on into the protoCore built-in
// prototypes, whose root `getPrototype` is self-referential, and spin forever.
//
// COL-d: a `Dictionary` is special. `collect:`/`select:` over a Dictionary map
// or filter its *values* (Dictionary iterates values), and the result cannot
// stay a Dictionary — the keys are gone. The protoST simplification (matching
// the spec) is that a Dictionary's species result-building yields an `Array`:
// `Dictionary` resolves to `arrayProto` here, so `collect:`/`select:` over a
// dictionary's values produce an `Array`. `select:` losing the keys is the
// documented limitation of this slice.
const proto::ProtoObject* speciesProtoOf(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r) {
    const proto::ProtoObject* ocProto    = rt.bootstrap().orderedCollectionProto;
    const proto::ProtoObject* arrayProto = rt.bootstrap().arrayProto;
    const proto::ProtoObject* setProto   = rt.bootstrap().setProto;
    const proto::ProtoObject* bagProto   = rt.bootstrap().bagProto;
    const proto::ProtoObject* dictProto  = rt.bootstrap().dictionaryProto;
    const proto::ProtoObject* objProto   = rt.bootstrap().objectProto;
    const proto::ProtoObject* prev       = nullptr;
    for (const proto::ProtoObject* p = r; p && p != PROTO_NONE && p != prev;
         prev = p, p = p->getPrototype(ctx)) {
        if (p == ocProto)    return ocProto;
        if (p == arrayProto) return arrayProto;
        if (p == setProto)   return setProto;
        if (p == bagProto)   return bagProto;
        // A Dictionary's derived results lose the keys — build an Array.
        if (p == dictProto)  return arrayProto;
        // Reached the protoST root — stop before the protoCore prototypes
        // (whose root is self-referential). An `Interval` falls through here.
        if (p == objProto)   break;
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
bool forEachElement(STRuntime& rt, proto::ProtoContext* ctx,
                    const proto::ProtoObject* collection, Fn&& fn) {
    // COL-e: an `Interval` is LAZY — it has no `__data__`. Detect it by
    // prototype identity and compute each element on demand: stride from
    // `start` by `step` while not past `stop`. After this arm the whole
    // derived protocol (`collect:`/`select:`/`inject:into:`/…) works on it.
    if (isIntervalBacked(rt, ctx, collection)) {
        long long start = intervalAttr(ctx, collection, intervalStartKey(ctx), 1);
        long long stop  = intervalAttr(ctx, collection, intervalStopKey(ctx), 0);
        long long step  = intervalAttr(ctx, collection, intervalStepKey(ctx), 1);
        long long n     = intervalSize(start, stop, step);
        long long cur   = start;
        for (long long i = 0; i < n; ++i, cur += step) {
            if (!fn(ctx->fromLong(cur))) return false;
        }
        return true;
    }
    if (isListBacked(ctx, collection)) {
        const proto::ProtoList* data = arrayData(ctx, collection);
        unsigned long n = data->getSize(ctx);
        for (unsigned long i = 0; i < n; ++i) {
            const proto::ProtoObject* e = data->getAt(ctx, static_cast<int>(i));
            if (!fn(e ? e : PROTO_NONE)) return false;
        }
        return true;
    }
    // COL-c: a `Set` is ProtoSet-backed — iterate it via ProtoSetIterator,
    // visiting each distinct element once.
    if (isSetBacked(ctx, collection)) {
        const proto::ProtoSet* data = setDataOf(ctx, collection);
        const proto::ProtoSetIterator* it = data->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* e = it->next(ctx);
            if (!fn(e ? e : PROTO_NONE)) return false;
            it = it->advance(ctx);
        }
        return true;
    }
    // COL-d: a `Dictionary` is ProtoSparseList-backed (hash -> bucket). Iterating
    // a Dictionary visits its VALUES — consistent with `Dictionary>>do:` and the
    // Smalltalk convention. Walk every bucket, and within each bucket every
    // [key, value] pair, yielding the value. After this the derived protocol
    // (`inject:into:`, `detect:`, `collect:`, …) works on a Dictionary over its
    // values.
    if (isDictBacked(ctx, collection)) {
        const proto::ProtoSparseList* data = dictData(ctx, collection);
        const proto::ProtoSparseListIterator* it = data->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* bucketObj = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            const proto::ProtoList* bucket =
                bucketObj ? bucketObj->asList(ctx) : nullptr;
            if (!bucket) continue;
            unsigned long bn = bucket->getSize(ctx);
            for (unsigned long i = 1; i < bn; i += 2) {  // values at odd slots
                const proto::ProtoObject* v =
                    bucket->getAt(ctx, static_cast<int>(i));
                if (!fn(v ? v : PROTO_NONE)) return false;
            }
        }
        return true;
    }
    // COL-c: a `Bag` is ProtoList-backed (one slot per occurrence) — it flows
    // through the ProtoList arm above. COL-e: the `Interval` kind is handled
    // by the lazy arm at the top of this function (it has no `__data__`).
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

// =========================  Set base operations  ===========================
//
// A `Set` is a hashed collection of unique elements. It uses the same
// mutable-holder representation as the sequenceable collections — a mutable
// child of the `Set` prototype with `__data__` — but `__data__` holds an
// immutable protoCore `ProtoSet` (wrapped via `asObject`) rather than a
// `ProtoList`. Every mutator (`add:`, `remove:`) swaps `__data__` for the new
// snapshot the protoCore mutator returns. Element equality / hashing for
// membership is protoCore's own (handled inside `ProtoSet::add`/`has`/`remove`).
//
// Because the backing store is a `ProtoSet`, `Set` flows through
// forEachElement's ProtoSet arm, so the whole derived protocol works on it.

// makeSetInstance — build a fresh Set instance wrapping `data`.
const proto::ProtoObject* makeSetInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoSet* data) {
    if (!data) data = ctx->newSet();
    TransientPin pinData(
        ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    const proto::ProtoObject* s =
        const_cast<proto::ProtoObject*>(rt.bootstrap().setProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinS(ctx, s);
    const_cast<proto::ProtoObject*>(s)->setAttribute(
        ctx, dataKey(ctx), data->asObject(ctx));
    return s;
}

// aSet add: anObject → add the element; return it. Adding a duplicate is a
// no-op — ProtoSet dedups.
const proto::ProtoObject* prim_Set_add(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("add: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    setSetData(ctx, r, setDataOf(ctx, r)->add(ctx, e));
    return e;
}

// aSet remove: anObject → remove the element; return it. Not present signals
// an Error.
const proto::ProtoObject* prim_Set_remove(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("remove: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    const proto::ProtoSet* data = setDataOf(ctx, r);
    if (data->has(ctx, e) != PROTO_TRUE)
        throw std::runtime_error("Set>>remove:: element not found");
    setSetData(ctx, r, data->remove(ctx, e));
    return e;
}

// aSet remove: anObject ifAbsent: aBlock → as remove:, but on a miss evaluate
// `aBlock` (no args) and return its value instead of signalling.
const proto::ProtoObject* prim_Set_removeIfAbsent(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 2)
        throw std::runtime_error("remove:ifAbsent: expects 2 args (element, block)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    const proto::ProtoSet* data = setDataOf(ctx, r);
    if (data->has(ctx, e) != PROTO_TRUE) {
        const proto::ProtoObject* fallback = invokeBlock(rt, ctx, a[1], nullptr, 0);
        return fallback ? fallback : PROTO_NONE;
    }
    setSetData(ctx, r, data->remove(ctx, e));
    return e;
}

// aSet includes: anObject → true when the element is a member.
const proto::ProtoObject* prim_Set_includes(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("includes: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    return setDataOf(ctx, r)->has(ctx, e) == PROTO_TRUE ? PROTO_TRUE : PROTO_FALSE;
}

// aSet size → number of distinct elements.
const proto::ProtoObject* prim_Set_size(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    return ctx->fromLong(
        static_cast<long long>(setDataOf(ctx, r)->getSize(ctx)));
}

const proto::ProtoObject* prim_Set_isEmpty(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    return setDataOf(ctx, r)->getSize(ctx) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_Set_notEmpty(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    return setDataOf(ctx, r)->getSize(ctx) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

// aSet do: aBlock → evaluate the one-arg block once for each distinct element.
const proto::ProtoObject* prim_Set_do(STRuntime& rt, proto::ProtoContext* ctx,
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

// Set new → a fresh empty Set.
const proto::ProtoObject* prim_Set_classNew(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* /*cls*/,
                                            const proto::ProtoObject* const*, int) {
    return makeSetInstance(rt, ctx, ctx->newSet());
}

// Set withAll: aCollection → a Set of the distinct elements of the argument
// (any collection the iteration protocol understands).
const proto::ProtoObject* prim_Set_classWithAll(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* /*cls*/,
                                                const proto::ProtoObject* const* a,
                                                int argc) {
    if (argc != 1) throw std::runtime_error("withAll: expects 1 arg (collection)");
    const proto::ProtoSet* data = ctx->newSet();
    TransientPin pinData(ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    forEachElement(rt, ctx, a[0], [&](const proto::ProtoObject* e) {
        data = data->add(ctx, e);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
        return true;
    });
    return makeSetInstance(rt, ctx, data);
}

// =========================  Bag base operations  ===========================
//
// A `Bag` is a hashed collection that counts occurrences. It uses the same
// mutable-holder representation, but — unlike `Set` — `__data__` holds a
// `ProtoList` with ONE slot per occurrence (an element added three times
// occupies three slots). protoCore's `ProtoMultiset` cannot back a `Bag`: it
// stores element->count keyed by the element's HASH and never retains the
// element object, so a ProtoMultiset cannot be iterated to recover elements.
// A ProtoList of occurrences gives correct `do:` / `occurrencesOf:` and lets
// the whole derived protocol work — `Bag` flows through forEachElement's
// ProtoList arm. Element equality for `remove:` / `occurrencesOf:` uses
// protoCore's object comparison via `indexOfEqual` (declared above).

// makeBagInstance — build a fresh Bag instance wrapping `data` (one slot per
// occurrence). Routed through makeInstanceOfSpecies, which for the Bag
// prototype wraps a ProtoList directly.
const proto::ProtoObject* makeBagInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoList* data) {
    return makeInstanceOfSpecies(rt, ctx, rt.bootstrap().bagProto,
                                 data ? data : ctx->newList());
}

// aBag add: anObject → add one occurrence; return the element.
const proto::ProtoObject* prim_Bag_add(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("add: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    setData(ctx, r, listData(ctx, r)->appendLast(ctx, e));
    return e;
}

// aBag add: anObject withOccurrences: n → add N occurrences; return the element.
const proto::ProtoObject* prim_Bag_addWithOccurrences(STRuntime&, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* r,
                                                      const proto::ProtoObject* const* a,
                                                      int argc) {
    if (argc != 2)
        throw std::runtime_error("add:withOccurrences: expects 2 args (element, count)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    long long n = a[1]->asLong(ctx);
    if (n < 0) throw std::runtime_error("Bag>>add:withOccurrences:: negative count");
    const proto::ProtoList* data = listData(ctx, r);
    TransientPin pinData(ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    for (long long i = 0; i < n; ++i) {
        data = data->appendLast(ctx, e);
        pinData.reset(reinterpret_cast<const proto::ProtoObject*>(data));
    }
    setData(ctx, r, data);
    return e;
}

// aBag remove: anObject → remove one occurrence; return the element. Absent
// signals an Error.
const proto::ProtoObject* prim_Bag_remove(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("remove: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    const proto::ProtoList* data = listData(ctx, r);
    int idx = indexOfEqual(ctx, data, e);
    if (idx < 0)
        throw std::runtime_error("Bag>>remove:: element not found");
    setData(ctx, r, data->removeAt(ctx, idx));
    return e;
}

// aBag remove: anObject ifAbsent: aBlock → as remove:, but on a miss evaluate
// `aBlock` (no args) and return its value instead of signalling.
const proto::ProtoObject* prim_Bag_removeIfAbsent(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a,
                                                  int argc) {
    if (argc != 2)
        throw std::runtime_error("remove:ifAbsent: expects 2 args (element, block)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    const proto::ProtoList* data = listData(ctx, r);
    int idx = indexOfEqual(ctx, data, e);
    if (idx < 0) {
        const proto::ProtoObject* fallback = invokeBlock(rt, ctx, a[1], nullptr, 0);
        return fallback ? fallback : PROTO_NONE;
    }
    setData(ctx, r, data->removeAt(ctx, idx));
    return e;
}

// Count how many slots of `data` are equal to `value`, using protoCore's
// object comparison — the per-occurrence count.
long long countEqual(proto::ProtoContext* ctx, const proto::ProtoList* data,
                     const proto::ProtoObject* value) {
    if (!value) value = PROTO_NONE;
    long long n = 0;
    unsigned long sz = data->getSize(ctx);
    for (unsigned long i = 0; i < sz; ++i) {
        const proto::ProtoObject* e = data->getAt(ctx, static_cast<int>(i));
        if (!e) e = PROTO_NONE;
        if (e == value || e->compare(ctx, value) == 0) ++n;
    }
    return n;
}

// aBag includes: anObject → true when at least one occurrence is present.
const proto::ProtoObject* prim_Bag_includes(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("includes: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    return indexOfEqual(ctx, listData(ctx, r), e) >= 0 ? PROTO_TRUE : PROTO_FALSE;
}

// aBag occurrencesOf: anObject → the occurrence count for the element.
const proto::ProtoObject* prim_Bag_occurrencesOf(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a,
                                                 int argc) {
    if (argc != 1) throw std::runtime_error("occurrencesOf: expects 1 arg (element)");
    const proto::ProtoObject* e = a[0] ? a[0] : PROTO_NONE;
    return ctx->fromLong(countEqual(ctx, listData(ctx, r), e));
}

// aBag size → total number of elements, duplicates included.
const proto::ProtoObject* prim_Bag_size(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    return ctx->fromLong(
        static_cast<long long>(listData(ctx, r)->getSize(ctx)));
}

const proto::ProtoObject* prim_Bag_isEmpty(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    return listData(ctx, r)->getSize(ctx) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_Bag_notEmpty(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    return listData(ctx, r)->getSize(ctx) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

// aBag do: aBlock → evaluate the one-arg block once per occurrence.
const proto::ProtoObject* prim_Bag_do(STRuntime& rt, proto::ProtoContext* ctx,
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

// Bag new → a fresh empty Bag.
const proto::ProtoObject* prim_Bag_classNew(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* /*cls*/,
                                            const proto::ProtoObject* const*, int) {
    return makeBagInstance(rt, ctx, ctx->newList());
}

// Bag withAll: aCollection → a Bag of every element of the argument (any
// collection the iteration protocol understands), keeping duplicates.
const proto::ProtoObject* prim_Bag_classWithAll(STRuntime& rt, proto::ProtoContext* ctx,
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
    return makeBagInstance(rt, ctx, data);
}

// =========================  Association  ===================================
//
// An `Association` is a minimal key->value pair — NOT a collection (it is a
// direct child of `Object`). It exists to support `Dictionary>>associationsDo:`
// and the `aKey -> aValue` literal. The key and value are plain attributes
// (`__assoc_key__` / `__assoc_value__`); `key`/`value`/`key:`/`value:` are
// accessors.

const proto::ProtoString* assocKeyKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__assoc_key__");
}
const proto::ProtoString* assocValueKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__assoc_value__");
}

// makeAssociation — build a fresh Association instance for `key -> value`.
const proto::ProtoObject* makeAssociation(STRuntime& rt, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* key,
                                          const proto::ProtoObject* value) {
    if (!key)   key   = PROTO_NONE;
    if (!value) value = PROTO_NONE;
    TransientPin pinKey(ctx, key);
    TransientPin pinValue(ctx, value);
    const proto::ProtoObject* assoc =
        const_cast<proto::ProtoObject*>(rt.bootstrap().associationProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinAssoc(ctx, assoc);
    const_cast<proto::ProtoObject*>(assoc)->setAttribute(
        ctx, assocKeyKey(ctx), key);
    const_cast<proto::ProtoObject*>(assoc)->setAttribute(
        ctx, assocValueKey(ctx), value);
    return assoc;
}

// aKey -> aValue → a fresh Association. Bound as a binary primitive on
// `objectProto`, so any object is a valid association key.
const proto::ProtoObject* prim_Object_arrow(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a,
                                            int argc) {
    if (argc != 1) throw std::runtime_error("-> expects 1 arg (value)");
    return makeAssociation(rt, ctx, r, a[0]);
}

// anAssociation key → the key.
const proto::ProtoObject* prim_Assoc_key(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    const proto::ProtoObject* k = r ? r->getAttribute(ctx, assocKeyKey(ctx)) : nullptr;
    return k ? k : PROTO_NONE;
}

// anAssociation value → the value.
const proto::ProtoObject* prim_Assoc_value(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    const proto::ProtoObject* v = r ? r->getAttribute(ctx, assocValueKey(ctx)) : nullptr;
    return v ? v : PROTO_NONE;
}

// anAssociation key: aKey → set the key; return the receiver.
const proto::ProtoObject* prim_Assoc_keyPut(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("key: expects 1 arg");
    const_cast<proto::ProtoObject*>(r)->setAttribute(
        ctx, assocKeyKey(ctx), a[0] ? a[0] : PROTO_NONE);
    return r;
}

// anAssociation value: aValue → set the value; return the receiver.
const proto::ProtoObject* prim_Assoc_valuePut(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("value: expects 1 arg");
    const_cast<proto::ProtoObject*>(r)->setAttribute(
        ctx, assocValueKey(ctx), a[0] ? a[0] : PROTO_NONE);
    return r;
}

// =========================  Dictionary base operations  ====================
//
// A `Dictionary` is a hashed key->value map with arbitrary object keys. It
// uses the mutable-holder representation — a mutable child of the `Dictionary`
// prototype with `__data__` — but `__data__` holds a `ProtoSparseList` keyed
// by `key->getHash(ctx)`, each slot a bucket `ProtoList` of alternating
// `[key0, value0, key1, value1, …]` (see the dictData comment block above).
// Lookup: hash → bucket → linear scan comparing keys by protoCore object
// equality. Every mutator swaps `__data__` for the new ProtoSparseList.

// Scan a bucket for `key`; return the index of its key slot (an even index),
// or -1 if absent. Keys are compared by protoCore object equality, with a
// pointer-identity fast path (matches Bag's indexOfEqual).
int bucketIndexOfKey(proto::ProtoContext* ctx, const proto::ProtoList* bucket,
                     const proto::ProtoObject* key) {
    if (!bucket) return -1;
    if (!key) key = PROTO_NONE;
    unsigned long n = bucket->getSize(ctx);
    for (unsigned long i = 0; i < n; i += 2) {
        const proto::ProtoObject* k = bucket->getAt(ctx, static_cast<int>(i));
        if (!k) k = PROTO_NONE;
        if (k == key || k->compare(ctx, key) == 0) return static_cast<int>(i);
    }
    return -1;
}

// Look up `key` in `data`. On a hit, returns the value and sets `*found`;
// on a miss returns PROTO_NONE with `*found` false.
const proto::ProtoObject* dictLookup(proto::ProtoContext* ctx,
                                     const proto::ProtoSparseList* data,
                                     const proto::ProtoObject* key, bool* found) {
    *found = false;
    if (!key) key = PROTO_NONE;
    unsigned long h = dictKeyHash(ctx, key);
    if (!data->has(ctx, h)) return PROTO_NONE;
    const proto::ProtoObject* bucketObj = data->getAt(ctx, h);
    const proto::ProtoList* bucket = bucketObj ? bucketObj->asList(ctx) : nullptr;
    int ki = bucketIndexOfKey(ctx, bucket, key);
    if (ki < 0) return PROTO_NONE;
    *found = true;
    const proto::ProtoObject* v = bucket->getAt(ctx, ki + 1);
    return v ? v : PROTO_NONE;
}

// Count of entries across every bucket.
long long dictSize(proto::ProtoContext* ctx, const proto::ProtoSparseList* data) {
    long long n = 0;
    const proto::ProtoSparseListIterator* it = data->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* bucketObj = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoList* bucket =
            bucketObj ? bucketObj->asList(ctx) : nullptr;
        if (bucket) n += static_cast<long long>(bucket->getSize(ctx)) / 2;
    }
    return n;
}

// makeDictInstance — build a fresh Dictionary instance wrapping `data`.
const proto::ProtoObject* makeDictInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoSparseList* data) {
    if (!data) data = ctx->newSparseList();
    TransientPin pinData(ctx, reinterpret_cast<const proto::ProtoObject*>(data));
    const proto::ProtoObject* d =
        const_cast<proto::ProtoObject*>(rt.bootstrap().dictionaryProto)
            ->newChild(ctx, /*isMutable=*/true);
    TransientPin pinD(ctx, d);
    const_cast<proto::ProtoObject*>(d)->setAttribute(
        ctx, dataKey(ctx), data->asObject(ctx));
    return d;
}

// Store `key -> value` into `data`, returning the updated ProtoSparseList.
// Replaces the value if the key is already present, else appends to the bucket.
const proto::ProtoSparseList* dictStore(proto::ProtoContext* ctx,
                                        const proto::ProtoSparseList* data,
                                        const proto::ProtoObject* key,
                                        const proto::ProtoObject* value) {
    if (!key)   key   = PROTO_NONE;
    if (!value) value = PROTO_NONE;
    unsigned long h = dictKeyHash(ctx, key);
    const proto::ProtoList* bucket =
        data->has(ctx, h) ? data->getAt(ctx, h)->asList(ctx) : nullptr;
    if (!bucket) bucket = ctx->newList();
    int ki = bucketIndexOfKey(ctx, bucket, key);
    if (ki >= 0) {
        bucket = bucket->setAt(ctx, ki + 1, value);   // overwrite the value
    } else {
        bucket = bucket->appendLast(ctx, key);        // append [key, value]
        bucket = bucket->appendLast(ctx, value);
    }
    TransientPin pinBucket(ctx, reinterpret_cast<const proto::ProtoObject*>(bucket));
    return data->setAt(ctx, h, bucket->asObject(ctx));
}

// aDictionary at: key → the value. Absent key signals an Error (catchable).
const proto::ProtoObject* prim_Dict_at(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("at: expects 1 arg (key)");
    bool found = false;
    const proto::ProtoObject* v = dictLookup(ctx, dictData(ctx, r), a[0], &found);
    if (!found) throw std::runtime_error("Dictionary>>at:: key not found");
    return v;
}

// aDictionary at: key ifAbsent: aBlock → the value, or aBlock value on a miss.
const proto::ProtoObject* prim_Dict_atIfAbsent(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* r,
                                               const proto::ProtoObject* const* a, int argc) {
    if (argc != 2)
        throw std::runtime_error("at:ifAbsent: expects 2 args (key, block)");
    bool found = false;
    const proto::ProtoObject* v = dictLookup(ctx, dictData(ctx, r), a[0], &found);
    if (found) return v;
    const proto::ProtoObject* fallback = invokeBlock(rt, ctx, a[1], nullptr, 0);
    return fallback ? fallback : PROTO_NONE;
}

// aDictionary at: key ifAbsentPut: aBlock → the value if present, else store
// `aBlock value` under `key` and return it.
const proto::ProtoObject* prim_Dict_atIfAbsentPut(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const proto::ProtoObject* const* a, int argc) {
    if (argc != 2)
        throw std::runtime_error("at:ifAbsentPut: expects 2 args (key, block)");
    bool found = false;
    const proto::ProtoSparseList* data = dictData(ctx, r);
    const proto::ProtoObject* v = dictLookup(ctx, data, a[0], &found);
    if (found) return v;
    const proto::ProtoObject* fresh = invokeBlock(rt, ctx, a[1], nullptr, 0);
    if (!fresh) fresh = PROTO_NONE;
    TransientPin pinFresh(ctx, fresh);
    setDictData(ctx, r, dictStore(ctx, dictData(ctx, r), a[0], fresh));
    return fresh;
}

// aDictionary at: key put: value → store the entry; return `value`.
const proto::ProtoObject* prim_Dict_atPut(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int argc) {
    if (argc != 2)
        throw std::runtime_error("at:put: expects 2 args (key, value)");
    const proto::ProtoObject* value = a[1] ? a[1] : PROTO_NONE;
    setDictData(ctx, r, dictStore(ctx, dictData(ctx, r), a[0], value));
    return value;
}

// Remove `key` from `data`. On a hit returns the value via `*out` and the
// updated ProtoSparseList; on a miss `*out` is null and `data` is returned.
const proto::ProtoSparseList* dictRemove(proto::ProtoContext* ctx,
                                         const proto::ProtoSparseList* data,
                                         const proto::ProtoObject* key,
                                         const proto::ProtoObject** out) {
    *out = nullptr;
    if (!key) key = PROTO_NONE;
    unsigned long h = dictKeyHash(ctx, key);
    if (!data->has(ctx, h)) return data;
    const proto::ProtoList* bucket = data->getAt(ctx, h)->asList(ctx);
    int ki = bucketIndexOfKey(ctx, bucket, key);
    if (ki < 0) return data;
    const proto::ProtoObject* v = bucket->getAt(ctx, ki + 1);
    *out = v ? v : PROTO_NONE;
    // Drop the [key, value] pair — remove the value slot first so the key
    // slot index stays valid.
    bucket = bucket->removeAt(ctx, ki + 1);
    bucket = bucket->removeAt(ctx, ki);
    if (bucket->getSize(ctx) == 0) {
        return data->removeAt(ctx, h);            // bucket empty — drop the slot
    }
    TransientPin pinBucket(ctx, reinterpret_cast<const proto::ProtoObject*>(bucket));
    return data->setAt(ctx, h, bucket->asObject(ctx));
}

// aDictionary removeKey: key → remove the entry, return the value. Absent key
// signals an Error.
const proto::ProtoObject* prim_Dict_removeKey(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("removeKey: expects 1 arg (key)");
    const proto::ProtoObject* removed = nullptr;
    const proto::ProtoSparseList* updated =
        dictRemove(ctx, dictData(ctx, r), a[0], &removed);
    if (!removed) throw std::runtime_error("Dictionary>>removeKey:: key not found");
    setDictData(ctx, r, updated);
    return removed;
}

// aDictionary removeKey: key ifAbsent: aBlock → as removeKey:, but on a miss
// evaluate `aBlock` (no args) and return its value.
const proto::ProtoObject* prim_Dict_removeKeyIfAbsent(STRuntime& rt, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* r,
                                                      const proto::ProtoObject* const* a,
                                                      int argc) {
    if (argc != 2)
        throw std::runtime_error("removeKey:ifAbsent: expects 2 args (key, block)");
    const proto::ProtoObject* removed = nullptr;
    const proto::ProtoSparseList* updated =
        dictRemove(ctx, dictData(ctx, r), a[0], &removed);
    if (!removed) {
        const proto::ProtoObject* fallback = invokeBlock(rt, ctx, a[1], nullptr, 0);
        return fallback ? fallback : PROTO_NONE;
    }
    setDictData(ctx, r, updated);
    return removed;
}

// aDictionary includesKey: key → true when the key has an entry.
const proto::ProtoObject* prim_Dict_includesKey(STRuntime&, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("includesKey: expects 1 arg (key)");
    bool found = false;
    dictLookup(ctx, dictData(ctx, r), a[0], &found);
    return found ? PROTO_TRUE : PROTO_FALSE;
}

// aDictionary includes: value → true when at least one entry has that value.
const proto::ProtoObject* prim_Dict_includes(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("includes: expects 1 arg (value)");
    const proto::ProtoObject* value = a[0] ? a[0] : PROTO_NONE;
    bool hit = false;
    forEachElement(rt, ctx, r, [&](const proto::ProtoObject* v) {
        if (v == value || v->compare(ctx, value) == 0) { hit = true; return false; }
        return true;
    });
    return hit ? PROTO_TRUE : PROTO_FALSE;
}

// aDictionary size → number of entries.
const proto::ProtoObject* prim_Dict_size(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    return ctx->fromLong(dictSize(ctx, dictData(ctx, r)));
}

const proto::ProtoObject* prim_Dict_isEmpty(STRuntime&, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const*, int) {
    return dictSize(ctx, dictData(ctx, r)) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* prim_Dict_notEmpty(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    return dictSize(ctx, dictData(ctx, r)) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

// dictForEachEntry — the shared bucket walk for keysDo:/valuesDo:/etc. Calls
// `fn(key, value)` for every entry; `fn` returns false to stop early.
template <typename Fn>
void dictForEachEntry(proto::ProtoContext* ctx, const proto::ProtoObject* r, Fn&& fn) {
    const proto::ProtoSparseList* data = dictData(ctx, r);
    const proto::ProtoSparseListIterator* it = data->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* bucketObj = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoList* bucket =
            bucketObj ? bucketObj->asList(ctx) : nullptr;
        if (!bucket) continue;
        unsigned long bn = bucket->getSize(ctx);
        for (unsigned long i = 0; i + 1 < bn; i += 2) {
            const proto::ProtoObject* k = bucket->getAt(ctx, static_cast<int>(i));
            const proto::ProtoObject* v = bucket->getAt(ctx, static_cast<int>(i + 1));
            if (!fn(k ? k : PROTO_NONE, v ? v : PROTO_NONE)) return;
        }
    }
}

// aDictionary keysDo: aBlock → evaluate the one-arg block for each key.
const proto::ProtoObject* prim_Dict_keysDo(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("keysDo: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    TransientPin pinRecv(ctx, r);
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject* k,
                                 const proto::ProtoObject*) {
        const proto::ProtoObject* arg0 = k;
        invokeBlock(rt, ctx, block, &arg0, 1);
        return true;
    });
    return r;
}

// aDictionary valuesDo: aBlock → evaluate the one-arg block for each value.
const proto::ProtoObject* prim_Dict_valuesDo(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("valuesDo: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    TransientPin pinRecv(ctx, r);
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject*,
                                 const proto::ProtoObject* v) {
        const proto::ProtoObject* arg0 = v;
        invokeBlock(rt, ctx, block, &arg0, 1);
        return true;
    });
    return r;
}

// aDictionary do: aBlock → iterates the VALUES (Smalltalk convention).
const proto::ProtoObject* prim_Dict_do(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    return prim_Dict_valuesDo(rt, ctx, r, a, argc);
}

// aDictionary keysAndValuesDo: aBlock → evaluate the two-arg block (key, value)
// for each entry.
const proto::ProtoObject* prim_Dict_keysAndValuesDo(STRuntime& rt, proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* r,
                                                    const proto::ProtoObject* const* a,
                                                    int argc) {
    if (argc != 1)
        throw std::runtime_error("keysAndValuesDo: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    TransientPin pinRecv(ctx, r);
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject* k,
                                 const proto::ProtoObject* v) {
        const proto::ProtoObject* args[2] = { k, v };
        invokeBlock(rt, ctx, block, args, 2);
        return true;
    });
    return r;
}

// aDictionary associationsDo: aBlock → evaluate the one-arg block for each
// entry, passing a fresh Association.
const proto::ProtoObject* prim_Dict_associationsDo(STRuntime& rt, proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* r,
                                                   const proto::ProtoObject* const* a,
                                                   int argc) {
    if (argc != 1)
        throw std::runtime_error("associationsDo: expects 1 arg (block)");
    const proto::ProtoObject* block = a[0];
    TransientPin pinRecv(ctx, r);
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject* k,
                                 const proto::ProtoObject* v) {
        const proto::ProtoObject* assoc = makeAssociation(rt, ctx, k, v);
        const proto::ProtoObject* arg0 = assoc;
        invokeBlock(rt, ctx, block, &arg0, 1);
        return true;
    });
    return r;
}

// aDictionary keys → a Set of the keys.
const proto::ProtoObject* prim_Dict_keys(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const*, int) {
    const proto::ProtoSet* set = ctx->newSet();
    TransientPin pinSet(ctx, reinterpret_cast<const proto::ProtoObject*>(set));
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject* k,
                                 const proto::ProtoObject*) {
        set = set->add(ctx, k);
        pinSet.reset(reinterpret_cast<const proto::ProtoObject*>(set));
        return true;
    });
    return makeSetInstance(rt, ctx, set);
}

// aDictionary values → an Array of the values.
const proto::ProtoObject* prim_Dict_values(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const*, int) {
    const proto::ProtoList* out = ctx->newList();
    TransientPin pinOut(ctx, reinterpret_cast<const proto::ProtoObject*>(out));
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject*,
                                 const proto::ProtoObject* v) {
        out = out->appendLast(ctx, v);
        pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        return true;
    });
    return makeArrayInstance(rt, ctx, out);
}

// aDictionary associations → an Array of Associations.
const proto::ProtoObject* prim_Dict_associations(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const*, int) {
    const proto::ProtoList* out = ctx->newList();
    TransientPin pinOut(ctx, reinterpret_cast<const proto::ProtoObject*>(out));
    dictForEachEntry(ctx, r, [&](const proto::ProtoObject* k,
                                 const proto::ProtoObject* v) {
        const proto::ProtoObject* assoc = makeAssociation(rt, ctx, k, v);
        out = out->appendLast(ctx, assoc);
        pinOut.reset(reinterpret_cast<const proto::ProtoObject*>(out));
        return true;
    });
    return makeArrayInstance(rt, ctx, out);
}

// Dictionary new → a fresh empty Dictionary.
const proto::ProtoObject* prim_Dict_classNew(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* /*cls*/,
                                             const proto::ProtoObject* const*, int) {
    return makeDictInstance(rt, ctx, ctx->newSparseList());
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

// =====================  Interval base operations  ==========================
//
// COL-e: an `Interval` is LAZY and READ-ONLY — no `add:` / `at:put:`. Each
// base operation reads the `start`/`stop`/`step` bounds and computes its
// answer; no element is ever materialised except by `do:` (one at a time) or
// the derived protocol (via `forEachElement`'s lazy arm).

// anInterval size → element count (a SmallInteger), never negative.
const proto::ProtoObject* prim_Interval_size(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    long long start = intervalAttr(ctx, r, intervalStartKey(ctx), 1);
    long long stop  = intervalAttr(ctx, r, intervalStopKey(ctx), 0);
    long long step  = intervalAttr(ctx, r, intervalStepKey(ctx), 1);
    return ctx->fromLong(intervalSize(start, stop, step));
}

// anInterval isEmpty → true when size = 0.
const proto::ProtoObject* prim_Interval_isEmpty(STRuntime&, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const*, int) {
    long long start = intervalAttr(ctx, r, intervalStartKey(ctx), 1);
    long long stop  = intervalAttr(ctx, r, intervalStopKey(ctx), 0);
    long long step  = intervalAttr(ctx, r, intervalStepKey(ctx), 1);
    return intervalSize(start, stop, step) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

// anInterval notEmpty → true when size > 0.
const proto::ProtoObject* prim_Interval_notEmpty(STRuntime&, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const*, int) {
    long long start = intervalAttr(ctx, r, intervalStartKey(ctx), 1);
    long long stop  = intervalAttr(ctx, r, intervalStopKey(ctx), 0);
    long long step  = intervalAttr(ctx, r, intervalStepKey(ctx), 1);
    return intervalSize(start, stop, step) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

// anInterval at: index → the 1-based element `start + (index-1)*step`. Out of
// range `1..size` signals an Error (via std::runtime_error → translate).
const proto::ProtoObject* prim_Interval_at(STRuntime&, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a,
                                           int argc) {
    if (argc != 1) throw std::runtime_error("at: expects 1 arg (index)");
    long long start = intervalAttr(ctx, r, intervalStartKey(ctx), 1);
    long long stop  = intervalAttr(ctx, r, intervalStopKey(ctx), 0);
    long long step  = intervalAttr(ctx, r, intervalStepKey(ctx), 1);
    long long n     = intervalSize(start, stop, step);
    long long idx1  = a[0]->asLong(ctx);             // 1-based
    if (idx1 < 1 || idx1 > n) {
        throw std::runtime_error(
            "Interval>>at:: index " + std::to_string(idx1) +
            " out of range 1.." + std::to_string(n));
    }
    return ctx->fromLong(start + (idx1 - 1) * step);
}

// anInterval first → the first element (`start`). An empty Interval signals
// an Error.
const proto::ProtoObject* prim_Interval_first(STRuntime&, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* r,
                                              const proto::ProtoObject* const*, int) {
    long long start = intervalAttr(ctx, r, intervalStartKey(ctx), 1);
    long long stop  = intervalAttr(ctx, r, intervalStopKey(ctx), 0);
    long long step  = intervalAttr(ctx, r, intervalStepKey(ctx), 1);
    if (intervalSize(start, stop, step) == 0)
        throw std::runtime_error("Interval>>first: interval is empty");
    return ctx->fromLong(start);
}

// anInterval last → the last element (`start + (size-1)*step`). An empty
// Interval signals an Error.
const proto::ProtoObject* prim_Interval_last(STRuntime&, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const*, int) {
    long long start = intervalAttr(ctx, r, intervalStartKey(ctx), 1);
    long long stop  = intervalAttr(ctx, r, intervalStopKey(ctx), 0);
    long long step  = intervalAttr(ctx, r, intervalStepKey(ctx), 1);
    long long n     = intervalSize(start, stop, step);
    if (n == 0)
        throw std::runtime_error("Interval>>last: interval is empty");
    return ctx->fromLong(start + (n - 1) * step);
}

// anInterval do: aBlock → lazily evaluate the one-arg block for each element,
// striding from `start` by `step`. Returns the receiver.
const proto::ProtoObject* prim_Interval_do(STRuntime& rt, proto::ProtoContext* ctx,
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

// =====================  Number iteration — to:/to:by:/to:do:/to:by:do:  ====
//
// COL-e: bound on `numberProto` so every numeric receiver inherits them
// (protoST currently has integers only, but the binding follows the shared
// prototype rather than `smallIntegerProto`). `to:` / `to:by:` build a lazy
// `Interval`; `to:do:` / `to:by:do:` iterate inline — the common Smalltalk
// loop — and return the receiver.

// aNumber to: stop → an Interval [receiver .. stop] with step 1.
const proto::ProtoObject* prim_Number_to(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const* a,
                                         int argc) {
    if (argc != 1) throw std::runtime_error("to: expects 1 arg (stop)");
    return makeIntervalInstance(rt, ctx, r->asLong(ctx), a[0]->asLong(ctx), 1);
}

// aNumber to: stop by: step → an Interval with the given stride.
const proto::ProtoObject* prim_Number_toBy(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a,
                                           int argc) {
    if (argc != 2) throw std::runtime_error("to:by: expects 2 args (stop, step)");
    return makeIntervalInstance(rt, ctx,
                                r->asLong(ctx), a[0]->asLong(ctx), a[1]->asLong(ctx));
}

// aNumber to: stop do: aBlock → iterate from the receiver by 1 while <= stop,
// evaluating `aBlock value: i`. Returns the receiver.
const proto::ProtoObject* prim_Number_toDo(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* r,
                                           const proto::ProtoObject* const* a,
                                           int argc) {
    if (argc != 2) throw std::runtime_error("to:do: expects 2 args (stop, block)");
    long long start = r->asLong(ctx);
    long long stop  = a[0]->asLong(ctx);
    const proto::ProtoObject* block = a[1];
    TransientPin pinRecv(ctx, r);
    for (long long i = start; i <= stop; ++i) {
        const proto::ProtoObject* arg0 = ctx->fromLong(i);
        invokeBlock(rt, ctx, block, &arg0, 1);
    }
    return r;
}

// aNumber to: stop by: step do: aBlock → iterate with an explicit stride. For
// `step > 0` while <= stop, for `step < 0` while >= stop. Returns the receiver.
const proto::ProtoObject* prim_Number_toByDo(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* r,
                                             const proto::ProtoObject* const* a,
                                             int argc) {
    if (argc != 3)
        throw std::runtime_error("to:by:do: expects 3 args (stop, step, block)");
    long long start = r->asLong(ctx);
    long long stop  = a[0]->asLong(ctx);
    long long step  = a[1]->asLong(ctx);
    const proto::ProtoObject* block = a[2];
    if (step == 0) throw std::runtime_error("to:by:do: step must be non-zero");
    TransientPin pinRecv(ctx, r);
    if (step > 0) {
        for (long long i = start; i <= stop; i += step) {
            const proto::ProtoObject* arg0 = ctx->fromLong(i);
            invokeBlock(rt, ctx, block, &arg0, 1);
        }
    } else {
        for (long long i = start; i >= stop; i += step) {
            const proto::ProtoObject* arg0 = ctx->fromLong(i);
            invokeBlock(rt, ctx, block, &arg0, 1);
        }
    }
    return r;
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

    // --- Interval base operations — bound on the Interval prototype --------
    // COL-e: a lazy, read-only sequenceable collection — no add:/at:put:.
    bindPrimitive(rt, b.intervalProto, "size",
                  reg.registerPrim(prim_Interval_size));
    bindPrimitive(rt, b.intervalProto, "isEmpty",
                  reg.registerPrim(prim_Interval_isEmpty));
    bindPrimitive(rt, b.intervalProto, "notEmpty",
                  reg.registerPrim(prim_Interval_notEmpty));
    bindPrimitive(rt, b.intervalProto, "at:",
                  reg.registerPrim(prim_Interval_at));
    bindPrimitive(rt, b.intervalProto, "first",
                  reg.registerPrim(prim_Interval_first));
    bindPrimitive(rt, b.intervalProto, "last",
                  reg.registerPrim(prim_Interval_last));
    bindPrimitive(rt, b.intervalProto, "do:",
                  reg.registerPrim(prim_Interval_do));

    // --- Number iteration — bound on numberProto, inherited by all numbers -
    // COL-e: `to:` / `to:by:` build a lazy Interval; `to:do:` / `to:by:do:`
    // iterate inline. Bound on `numberProto` (the shared numeric prototype)
    // so integers — and any future Float — inherit them uniformly.
    bindPrimitive(rt, b.numberProto, "to:",
                  reg.registerPrim(prim_Number_to));
    bindPrimitive(rt, b.numberProto, "to:by:",
                  reg.registerPrim(prim_Number_toBy));
    bindPrimitive(rt, b.numberProto, "to:do:",
                  reg.registerPrim(prim_Number_toDo));
    bindPrimitive(rt, b.numberProto, "to:by:do:",
                  reg.registerPrim(prim_Number_toByDo));

    // --- Set base operations — bound on the Set prototype ------------------
    bindPrimitive(rt, b.setProto, "add:",
                  reg.registerPrim(prim_Set_add));
    bindPrimitive(rt, b.setProto, "remove:",
                  reg.registerPrim(prim_Set_remove));
    bindPrimitive(rt, b.setProto, "remove:ifAbsent:",
                  reg.registerPrim(prim_Set_removeIfAbsent));
    bindPrimitive(rt, b.setProto, "includes:",
                  reg.registerPrim(prim_Set_includes));
    bindPrimitive(rt, b.setProto, "size",
                  reg.registerPrim(prim_Set_size));
    bindPrimitive(rt, b.setProto, "isEmpty",
                  reg.registerPrim(prim_Set_isEmpty));
    bindPrimitive(rt, b.setProto, "notEmpty",
                  reg.registerPrim(prim_Set_notEmpty));
    bindPrimitive(rt, b.setProto, "do:",
                  reg.registerPrim(prim_Set_do));
    // --- Set class-side constructors — bound on the Set prototype ----------
    bindPrimitive(rt, b.setProto, "new",
                  reg.registerPrim(prim_Set_classNew));
    bindPrimitive(rt, b.setProto, "withAll:",
                  reg.registerPrim(prim_Set_classWithAll));

    // --- Bag base operations — bound on the Bag prototype ------------------
    bindPrimitive(rt, b.bagProto, "add:",
                  reg.registerPrim(prim_Bag_add));
    bindPrimitive(rt, b.bagProto, "add:withOccurrences:",
                  reg.registerPrim(prim_Bag_addWithOccurrences));
    bindPrimitive(rt, b.bagProto, "remove:",
                  reg.registerPrim(prim_Bag_remove));
    bindPrimitive(rt, b.bagProto, "remove:ifAbsent:",
                  reg.registerPrim(prim_Bag_removeIfAbsent));
    bindPrimitive(rt, b.bagProto, "includes:",
                  reg.registerPrim(prim_Bag_includes));
    bindPrimitive(rt, b.bagProto, "occurrencesOf:",
                  reg.registerPrim(prim_Bag_occurrencesOf));
    bindPrimitive(rt, b.bagProto, "size",
                  reg.registerPrim(prim_Bag_size));
    bindPrimitive(rt, b.bagProto, "isEmpty",
                  reg.registerPrim(prim_Bag_isEmpty));
    bindPrimitive(rt, b.bagProto, "notEmpty",
                  reg.registerPrim(prim_Bag_notEmpty));
    bindPrimitive(rt, b.bagProto, "do:",
                  reg.registerPrim(prim_Bag_do));
    // --- Bag class-side constructors — bound on the Bag prototype ----------
    bindPrimitive(rt, b.bagProto, "new",
                  reg.registerPrim(prim_Bag_classNew));
    bindPrimitive(rt, b.bagProto, "withAll:",
                  reg.registerPrim(prim_Bag_classWithAll));

    // --- Dictionary base operations — bound on the Dictionary prototype ----
    bindPrimitive(rt, b.dictionaryProto, "at:",
                  reg.registerPrim(prim_Dict_at));
    bindPrimitive(rt, b.dictionaryProto, "at:ifAbsent:",
                  reg.registerPrim(prim_Dict_atIfAbsent));
    bindPrimitive(rt, b.dictionaryProto, "at:ifAbsentPut:",
                  reg.registerPrim(prim_Dict_atIfAbsentPut));
    bindPrimitive(rt, b.dictionaryProto, "at:put:",
                  reg.registerPrim(prim_Dict_atPut));
    bindPrimitive(rt, b.dictionaryProto, "removeKey:",
                  reg.registerPrim(prim_Dict_removeKey));
    bindPrimitive(rt, b.dictionaryProto, "removeKey:ifAbsent:",
                  reg.registerPrim(prim_Dict_removeKeyIfAbsent));
    bindPrimitive(rt, b.dictionaryProto, "includesKey:",
                  reg.registerPrim(prim_Dict_includesKey));
    bindPrimitive(rt, b.dictionaryProto, "includes:",
                  reg.registerPrim(prim_Dict_includes));
    bindPrimitive(rt, b.dictionaryProto, "size",
                  reg.registerPrim(prim_Dict_size));
    bindPrimitive(rt, b.dictionaryProto, "isEmpty",
                  reg.registerPrim(prim_Dict_isEmpty));
    bindPrimitive(rt, b.dictionaryProto, "notEmpty",
                  reg.registerPrim(prim_Dict_notEmpty));
    bindPrimitive(rt, b.dictionaryProto, "do:",
                  reg.registerPrim(prim_Dict_do));
    bindPrimitive(rt, b.dictionaryProto, "keysDo:",
                  reg.registerPrim(prim_Dict_keysDo));
    bindPrimitive(rt, b.dictionaryProto, "valuesDo:",
                  reg.registerPrim(prim_Dict_valuesDo));
    bindPrimitive(rt, b.dictionaryProto, "keysAndValuesDo:",
                  reg.registerPrim(prim_Dict_keysAndValuesDo));
    bindPrimitive(rt, b.dictionaryProto, "associationsDo:",
                  reg.registerPrim(prim_Dict_associationsDo));
    bindPrimitive(rt, b.dictionaryProto, "keys",
                  reg.registerPrim(prim_Dict_keys));
    bindPrimitive(rt, b.dictionaryProto, "values",
                  reg.registerPrim(prim_Dict_values));
    bindPrimitive(rt, b.dictionaryProto, "associations",
                  reg.registerPrim(prim_Dict_associations));
    // --- Dictionary class-side constructor ---------------------------------
    bindPrimitive(rt, b.dictionaryProto, "new",
                  reg.registerPrim(prim_Dict_classNew));

    // --- Association — bound on the Association prototype ------------------
    bindPrimitive(rt, b.associationProto, "key",
                  reg.registerPrim(prim_Assoc_key));
    bindPrimitive(rt, b.associationProto, "value",
                  reg.registerPrim(prim_Assoc_value));
    bindPrimitive(rt, b.associationProto, "key:",
                  reg.registerPrim(prim_Assoc_keyPut));
    bindPrimitive(rt, b.associationProto, "value:",
                  reg.registerPrim(prim_Assoc_valuePut));
    // `aKey -> aValue` builds an Association — bound on Object so any object
    // is a valid association key.
    bindPrimitive(rt, b.objectProto, "->",
                  reg.registerPrim(prim_Object_arrow));

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
