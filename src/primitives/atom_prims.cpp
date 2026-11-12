#include "protoST/STRuntime.h"
#include "runtime/NativeError.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <stdexcept>

namespace protoST {

// Defined in block_prims.cpp.
extern const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* block,
                                              const proto::ProtoObject* const* args,
                                              int argc);

// ===========================================================================
// Atom — a shared mutable cell with optimistic-concurrency compare-and-swap.
//
// An Atom is the *atom* half of Clojure's agent/atom pair: where an Actor
// serialises arbitrary logic over a piece of state, an Atom is a bare cell
// updated lock-free by a CAS-and-retry loop. Use an Atom when many threads /
// actors must update one shared value (a counter, a registry, a world graph)
// and routing every update through one owner actor would be a bottleneck.
//
// The cell holds its current value under the __atom_value__ attribute. Every
// update is `setAttributeIfEqual` — protoCore's atomic attribute CAS. Because
// the held values are immutable snapshots compared by pointer identity, the
// classic ABA hazard does not arise: an unchanged pointer means an unchanged
// value.
// ===========================================================================

namespace {

const proto::ProtoString* atomValueKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__atom_value__");
}

// anAtom __atomInit: anInitialValue  →  the receiver, its cell now holding
// anInitialValue. `Atom class >> on:` (lib/kernel/future.st) sends it to the
// instance `new` just built -- of Atom or of the subclass `on:` was sent to
// (S38) -- before the instance is shared, so a plain write is enough.
const proto::ProtoObject* prim_Atom_init(STRuntime&, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw NativeError("Atom __atomInit: expects 1 argument");
    const_cast<proto::ProtoObject*>(r)->setAttribute(ctx, atomValueKey(ctx), a[0] ? a[0] : PROTO_NONE);
    return r;
}

// anAtom value  →  the current snapshot held in the cell.
const proto::ProtoObject* prim_Atom_value(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const*, int) {
    auto* v = r->getOwnAttributeDirect(ctx, atomValueKey(ctx));
    return v ? v : PROTO_NONE;
}

// anAtom value: newValue ifCurrent: expected
//   The raw compare-and-swap. Installs `newValue` iff the cell still holds
//   `expected` (pointer identity); answers true on success, false on a lost
//   race — write nothing in that case. This is the primitive for callers that
//   want to drive their own validation / retry policy.
const proto::ProtoObject* prim_Atom_cas(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a, int argc) {
    if (argc != 2)
        throw NativeError("Atom value:ifCurrent: expects 2 arguments");
    const proto::ProtoObject* newValue = a[0] ? a[0] : PROTO_NONE;
    const proto::ProtoObject* expected = a[1] ? a[1] : PROTO_NONE;
    const proto::ProtoString* key = atomValueKey(ctx);
    // S39: a cell made by `new` holds nothing yet, and reads as nil. An
    // expected nil matches it, as an unset instance variable matches nil in
    // setInstVar:from:to:; protoCore's CAS names "absent" with nullptr.
    const proto::ProtoObject* casExpected =
        (expected == PROTO_NONE && r->getOwnAttributeDirect(ctx, key) == nullptr)
            ? nullptr : expected;
    bool ok = const_cast<proto::ProtoObject*>(r)
        ->setAttributeIfEqual(ctx, key, casExpected, newValue);
    return ok ? PROTO_TRUE : PROTO_FALSE;
}

// anAtom swap: aBlock
//   The convenience: a read-modify-CAS retry loop. Reads the current value,
//   applies `aBlock` to it to compute the new value, and CASes it in; on a
//   lost race it retries — re-reading the now-current value, so the block is
//   always applied to a fresh snapshot. Answers the value finally installed.
//   `aBlock` may run more than once and must therefore be side-effect free.
const proto::ProtoObject* prim_Atom_swap(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* r,
                                         const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw NativeError("Atom swap: expects 1 argument");
    const proto::ProtoObject* block = a[0];
    const proto::ProtoString* key = atomValueKey(ctx);
    for (;;) {
        // S39: a cell made by `new` holds nothing yet (nullptr). The block
        // sees nil, and the CAS expects the cell to be still empty: comparing
        // against an explicit nil never matched it, and swap: retried for
        // ever.
        const proto::ProtoObject* current = r->getOwnAttributeDirect(ctx, key);
        const proto::ProtoObject* old = current ? current : PROTO_NONE;
        // `old` is held across invokeBlock (arbitrary user allocation) and the
        // CAS; `neu` across the CAS. Pin both.
        TransientPin pinOld(ctx, old);
        const proto::ProtoObject* args[] = { old };
        const proto::ProtoObject* neu = invokeBlock(rt, ctx, block, args, 1);
        if (!neu) neu = PROTO_NONE;
        TransientPin pinNeu(ctx, neu);
        if (const_cast<proto::ProtoObject*>(r)
                ->setAttributeIfEqual(ctx, key, current, neu)) {
            return neu;
        }
        // CAS lost — another writer won since our read. Loop: re-read the new
        // current value and re-apply the block to it.
    }
}

} // anon

void installAtomPrimitives(STRuntime& rt) {
    auto& b   = rt.bootstrap();
    // `Atom on: 0` is `Atom class >> on:` in lib/kernel/future.st.
    bindPrimitive(rt, b.atomProto, "__atomInit:",
                  primitiveMethod<prim_Atom_init>);
    bindPrimitive(rt, b.atomProto, "value",
                  primitiveMethod<prim_Atom_value>);
    bindPrimitive(rt, b.atomProto, "value:ifCurrent:",
                  primitiveMethod<prim_Atom_cas>);
    bindPrimitive(rt, b.atomProto, "swap:",
                  primitiveMethod<prim_Atom_swap>);
}

} // namespace protoST
