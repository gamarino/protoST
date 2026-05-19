#include "Bootstrap.h"
#include "protoCore.h"

namespace protoST {

void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out) {
    // Root of the protoST class tree is a *mutable* child of protoCore's
    // objectPrototype.  Using a mutable proxy is required so that
    // `bindPrimitive` (which calls `setAttribute` and discards the result)
    // installs methods in place rather than returning a fresh COW copy that
    // the bootstrap pointer would not see — the latter would silently lose
    // every Object>>method binding (e.g. Object>>halt, Object>>printNl).
    out.objectProto       = sp.objectPrototype->newChild(ctx, /*isMutable=*/true);

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

    // F6 actor + future prototypes — mutable so methods can be bound.
    out.actorProto        = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);
    out.futureProto       = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);

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
    // Route nil (PROTO_NONE) through our nilProto so that selectors defined
    // on objectProto (e.g. Object>>halt, Object>>printNl) are reachable from
    // the nil receiver. nilProto's parent is objectProto so the walk succeeds.
    sp.nonePrototype         = const_cast<proto::ProtoObject*>(out.nilProto);
}

} // namespace protoST
