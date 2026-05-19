#include "Bootstrap.h"
#include "protoCore.h"

namespace protoST {

void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out) {
    // Root of the protoST class tree is protoCore's objectPrototype.
    out.objectProto       = sp.objectPrototype;

    // Number tree.  Prototypes are *mutable* so that `setAttribute` (used by
    // bindPrimitive) installs methods in place; otherwise immutable
    // setAttribute returns a new ProtoObject* and the bootstrap pointer (also
    // held in sp.smallIntegerPrototype) would be left bare.
    out.numberProto       = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.smallIntegerProto = out.numberProto->newChild(ctx, /*isMutable=*/true);
    out.largeIntegerProto = out.numberProto->newChild(ctx, /*isMutable=*/true);
    out.floatProto        = out.numberProto->newChild(ctx, /*isMutable=*/true);

    // Booleans, strings/symbols, blocks, nil.
    out.booleanProto      = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.stringProto       = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.symbolProto       = out.stringProto->newChild(ctx, /*isMutable=*/true);
    out.blockProto        = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.nilProto          = out.objectProto->newChild(ctx, /*isMutable=*/true);

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
