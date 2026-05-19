#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <stdexcept>

namespace protoST {

// invokeBlock is implemented in block_prims.cpp (Task 44). For now we ship a
// stub that throws if any code actually reaches BlockClosure>>value via
// ifTrue:/ifFalse:. Task 44 will move the real definition into block_prims.cpp
// and REMOVE this stub.
const proto::ProtoObject* invokeBlock(STRuntime&, proto::ProtoContext*,
                                       const proto::ProtoObject*,
                                       const proto::ProtoObject* const*, int) {
    throw std::runtime_error("BlockClosure>>value not yet installed (Task 44)");
}

namespace {

const proto::ProtoObject* prim_True_ifTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject*, const proto::ProtoObject* const* a, int) {
    return invokeBlock(rt, ctx, a[0], nullptr, 0);
}
const proto::ProtoObject* prim_False_ifTrue(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject*, const proto::ProtoObject* const*, int) {
    return PROTO_NONE;
}
const proto::ProtoObject* prim_True_ifFalse(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject*, const proto::ProtoObject* const*, int) {
    return PROTO_NONE;
}
const proto::ProtoObject* prim_False_ifFalse(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject*, const proto::ProtoObject* const* a, int) {
    return invokeBlock(rt, ctx, a[0], nullptr, 0);
}

const proto::ProtoObject* prim_dispatch_ifTrue(STRuntime& r, proto::ProtoContext* c, const proto::ProtoObject* recv,
                                                const proto::ProtoObject* const* a, int n) {
    return (recv == PROTO_TRUE) ? prim_True_ifTrue(r,c,recv,a,n) : prim_False_ifTrue(r,c,recv,a,n);
}
const proto::ProtoObject* prim_dispatch_ifFalse(STRuntime& r, proto::ProtoContext* c, const proto::ProtoObject* recv,
                                                 const proto::ProtoObject* const* a, int n) {
    return (recv == PROTO_TRUE) ? prim_True_ifFalse(r,c,recv,a,n) : prim_False_ifFalse(r,c,recv,a,n);
}

} // anon

void installBoolPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    int ifTrueIdx  = reg.registerPrim(prim_dispatch_ifTrue);
    int ifFalseIdx = reg.registerPrim(prim_dispatch_ifFalse);
    bindPrimitive(rt, b.booleanProto, "ifTrue:",  ifTrueIdx);
    bindPrimitive(rt, b.booleanProto, "ifFalse:", ifFalseIdx);
}

} // namespace protoST
