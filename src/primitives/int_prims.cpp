#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <stdexcept>

namespace protoST {

namespace {

#define DEFBIN_LONG(NAME, OP)                                                                       \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,                         \
                                       const proto::ProtoObject* r,                                 \
                                       const proto::ProtoObject* const* a, int argc) {              \
    if (argc != 1) throw std::runtime_error(#NAME " expects 1 arg");                                \
    long long x = r->asLong(ctx); long long y = a[0]->asLong(ctx);                                  \
    return ctx->fromLong(x OP y);                                                                   \
}

DEFBIN_LONG(IntAdd, +)
DEFBIN_LONG(IntSub, -)
DEFBIN_LONG(IntMul, *)

const proto::ProtoObject* prim_IntDiv(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("Int/ expects 1 arg");
    long long x = r->asLong(ctx); long long y = a[0]->asLong(ctx);
    if (y == 0) throw std::runtime_error("ZeroDivide");
    return ctx->fromLong(x / y);
}

#define DEFCMP(NAME, OP)                                                                            \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,                         \
                                       const proto::ProtoObject* r,                                 \
                                       const proto::ProtoObject* const* a, int) {                   \
    long long x = r->asLong(ctx); long long y = a[0]->asLong(ctx);                                  \
    return (x OP y) ? PROTO_TRUE : PROTO_FALSE;                                                     \
}
DEFCMP(IntLt, <) DEFCMP(IntLe, <=) DEFCMP(IntGt, >) DEFCMP(IntGe, >=)
DEFCMP(IntEq, ==) DEFCMP(IntNe, !=)

} // anon

void installIntPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    auto idxAdd = reg.registerPrim(prim_IntAdd); bindPrimitive(rt, b.smallIntegerProto, "+",  idxAdd);
    auto idxSub = reg.registerPrim(prim_IntSub); bindPrimitive(rt, b.smallIntegerProto, "-",  idxSub);
    auto idxMul = reg.registerPrim(prim_IntMul); bindPrimitive(rt, b.smallIntegerProto, "*",  idxMul);
    auto idxDiv = reg.registerPrim(prim_IntDiv); bindPrimitive(rt, b.smallIntegerProto, "/",  idxDiv);
    auto idxLt  = reg.registerPrim(prim_IntLt);  bindPrimitive(rt, b.smallIntegerProto, "<",  idxLt);
    auto idxLe  = reg.registerPrim(prim_IntLe);  bindPrimitive(rt, b.smallIntegerProto, "<=", idxLe);
    auto idxGt  = reg.registerPrim(prim_IntGt);  bindPrimitive(rt, b.smallIntegerProto, ">",  idxGt);
    auto idxGe  = reg.registerPrim(prim_IntGe);  bindPrimitive(rt, b.smallIntegerProto, ">=", idxGe);
    auto idxEq  = reg.registerPrim(prim_IntEq);  bindPrimitive(rt, b.smallIntegerProto, "=",  idxEq);
    auto idxNe  = reg.registerPrim(prim_IntNe);  bindPrimitive(rt, b.smallIntegerProto, "~=", idxNe);
}

} // namespace protoST
