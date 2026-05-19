#include "Bootstrap.h"
#include "protoCore.h"

namespace protoST {

void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out) {
    // Root of the protoST class tree is protoCore's objectPrototype.
    out.objectProto       = sp.objectPrototype;

    // Number tree.
    out.numberProto       = out.objectProto->newChild(ctx, /*isMutable=*/false);
    out.smallIntegerProto = out.numberProto->newChild(ctx, /*isMutable=*/false);
    out.largeIntegerProto = out.numberProto->newChild(ctx, /*isMutable=*/false);
    out.floatProto        = out.numberProto->newChild(ctx, /*isMutable=*/false);

    // Booleans, strings/symbols, blocks, nil.
    out.booleanProto      = out.objectProto->newChild(ctx, /*isMutable=*/false);
    out.stringProto       = out.objectProto->newChild(ctx, /*isMutable=*/false);
    out.symbolProto       = out.stringProto->newChild(ctx, /*isMutable=*/false);
    out.blockProto        = out.objectProto->newChild(ctx, /*isMutable=*/false);
    out.nilProto          = out.objectProto->newChild(ctx, /*isMutable=*/false);

    // Bind protoCore primitive slots so values produced by fromLong/fromDouble/etc.
    // walk up through our Smalltalk prototypes.  This mirrors protoJS's
    // NumberPrototype.cpp pattern (space->smallIntegerPrototype = const_cast<...>).
    //
    // Booleans (PROTO_TRUE/PROTO_FALSE) and PROTO_NONE remain bound to the
    // protoCore-built-in prototypes; once we attach our own primitives we will
    // re-point them.  For F2 the bare prototypes suffice.
    sp.smallIntegerPrototype = const_cast<proto::ProtoObject*>(out.smallIntegerProto);
    sp.largeIntegerPrototype = const_cast<proto::ProtoObject*>(out.largeIntegerProto);
    sp.floatPrototype        = const_cast<proto::ProtoObject*>(out.floatProto);
    sp.doublePrototype       = const_cast<proto::ProtoObject*>(out.floatProto);
    sp.stringPrototype       = const_cast<proto::ProtoObject*>(out.stringProto);
    sp.booleanPrototype      = const_cast<proto::ProtoObject*>(out.booleanProto);
}

} // namespace protoST
