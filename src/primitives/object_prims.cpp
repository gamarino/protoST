#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

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

} // anon

void installObjectPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.objectProto, "newChild",
                  reg.registerPrim(prim_Object_newChild));
}

} // namespace protoST
