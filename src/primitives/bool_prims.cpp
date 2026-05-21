#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <stdexcept>

namespace protoST {

// invokeBlock is defined in block_prims.cpp (Task 44). The forward declaration
// lets bool_prims.cpp call into it via external linkage.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc);

namespace {

// --- ifTrue: / ifFalse: ----------------------------------------------------

const proto::ProtoObject* prim_dispatch_ifTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* recv,
                                                const proto::ProtoObject* const* a, int) {
    return (recv == PROTO_TRUE) ? invokeBlock(rt, ctx, a[0], nullptr, 0)
                                : PROTO_NONE;
}
const proto::ProtoObject* prim_dispatch_ifFalse(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* recv,
                                                 const proto::ProtoObject* const* a, int) {
    return (recv == PROTO_TRUE) ? PROTO_NONE
                                : invokeBlock(rt, ctx, a[0], nullptr, 0);
}

// --- ifTrue:ifFalse: / ifFalse:ifTrue: -------------------------------------
//
// D9: a two-armed conditional. Exactly one of the two argument blocks is
// evaluated; the other is left untouched. Returns the value of the chosen
// block.
const proto::ProtoObject* prim_dispatch_ifTrueIfFalse(STRuntime& rt, proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* recv,
                                                       const proto::ProtoObject* const* a, int) {
    return (recv == PROTO_TRUE) ? invokeBlock(rt, ctx, a[0], nullptr, 0)
                                : invokeBlock(rt, ctx, a[1], nullptr, 0);
}
const proto::ProtoObject* prim_dispatch_ifFalseIfTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* recv,
                                                       const proto::ProtoObject* const* a, int) {
    return (recv == PROTO_TRUE) ? invokeBlock(rt, ctx, a[1], nullptr, 0)
                                : invokeBlock(rt, ctx, a[0], nullptr, 0);
}

// --- and: / or: (short-circuit) --------------------------------------------
//
// D9: `and:` evaluates its argument block only when the receiver is true;
// `or:` evaluates it only when the receiver is false. The argument is a block
// (standard Smalltalk) and is invoked with no arguments; its value becomes the
// result. When the result is already determined the block is NOT evaluated.
const proto::ProtoObject* prim_dispatch_and(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject* recv,
                                             const proto::ProtoObject* const* a, int) {
    if (recv != PROTO_TRUE) return PROTO_FALSE;          // short-circuit
    return invokeBlock(rt, ctx, a[0], nullptr, 0);
}
const proto::ProtoObject* prim_dispatch_or(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* recv,
                                            const proto::ProtoObject* const* a, int) {
    if (recv == PROTO_TRUE) return PROTO_TRUE;           // short-circuit
    return invokeBlock(rt, ctx, a[0], nullptr, 0);
}

// --- & / | (non-short-circuit) ---------------------------------------------
//
// D9: eager boolean conjunction / disjunction. The argument is an already
// evaluated Boolean, not a block.
const proto::ProtoObject* prim_dispatch_amp(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject* recv,
                                             const proto::ProtoObject* const* a, int) {
    return (recv == PROTO_TRUE && a[0] == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_dispatch_pipe(STRuntime&, proto::ProtoContext*,
                                              const proto::ProtoObject* recv,
                                              const proto::ProtoObject* const* a, int) {
    return (recv == PROTO_TRUE || a[0] == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_dispatch_xor(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject* recv,
                                             const proto::ProtoObject* const* a, int) {
    bool l = (recv == PROTO_TRUE), r = (a[0] == PROTO_TRUE);
    return (l != r) ? PROTO_TRUE : PROTO_FALSE;
}

// --- not -------------------------------------------------------------------
const proto::ProtoObject* prim_dispatch_not(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject* recv,
                                             const proto::ProtoObject* const*, int) {
    return (recv == PROTO_TRUE) ? PROTO_FALSE : PROTO_TRUE;
}

// --- = on Boolean ----------------------------------------------------------
//
// D18: Booleans are tagged immediates, so value-equality is identity.
const proto::ProtoObject* prim_dispatch_boolEq(STRuntime&, proto::ProtoContext*,
                                                const proto::ProtoObject* recv,
                                                const proto::ProtoObject* const* a, int) {
    return (recv == a[0]) ? PROTO_TRUE : PROTO_FALSE;
}

} // anon

void installBoolPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.booleanProto, "ifTrue:",
                  reg.registerPrim(prim_dispatch_ifTrue));
    bindPrimitive(rt, b.booleanProto, "ifFalse:",
                  reg.registerPrim(prim_dispatch_ifFalse));
    // D9: two-armed conditionals and the boolean combinators.
    bindPrimitive(rt, b.booleanProto, "ifTrue:ifFalse:",
                  reg.registerPrim(prim_dispatch_ifTrueIfFalse));
    bindPrimitive(rt, b.booleanProto, "ifFalse:ifTrue:",
                  reg.registerPrim(prim_dispatch_ifFalseIfTrue));
    bindPrimitive(rt, b.booleanProto, "and:",
                  reg.registerPrim(prim_dispatch_and));
    bindPrimitive(rt, b.booleanProto, "or:",
                  reg.registerPrim(prim_dispatch_or));
    bindPrimitive(rt, b.booleanProto, "&",
                  reg.registerPrim(prim_dispatch_amp));
    bindPrimitive(rt, b.booleanProto, "|",
                  reg.registerPrim(prim_dispatch_pipe));
    bindPrimitive(rt, b.booleanProto, "xor:",
                  reg.registerPrim(prim_dispatch_xor));
    bindPrimitive(rt, b.booleanProto, "not",
                  reg.registerPrim(prim_dispatch_not));
    // D18: value-equality on Boolean (identity for tagged immediates).
    bindPrimitive(rt, b.booleanProto, "=",
                  reg.registerPrim(prim_dispatch_boolEq));
}

} // namespace protoST
