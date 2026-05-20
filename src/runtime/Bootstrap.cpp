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

    // Track 1 slice 2 (EXC-a): exception class hierarchy. Ordinary mutable
    // prototypes — `Exception subclass: #MyError` works through the standard
    // `subclass:`/`newChild` path with no special-casing. `signal`, `on:do:`
    // and `return:` are bound later by installExceptionPrimitives.
    out.exceptionProto    = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);
    out.errorProto        = const_cast<proto::ProtoObject*>(out.exceptionProto)->newChild(ctx, /*isMutable=*/true);
    out.warningProto      = const_cast<proto::ProtoObject*>(out.exceptionProto)->newChild(ctx, /*isMutable=*/true);

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

    // BL-3: stamp each bootstrap prototype with its class name under the
    // perpetual `__class_name__` symbol key. printString (object_prims.cpp)
    // walks the prototype chain looking for this attribute to build a
    // human-readable "a ClassName" string. Built-in "classes" are just these
    // prototypes, so an Actor instance can print as "an Actor", etc.
    //
    // The key is a strong symbol — eternal vocabulary shared by every class
    // object — so it is created once via ProtoString::createSymbol and never
    // re-interned. The name values are short ProtoStrings created here; they
    // are attributes of the (permanently rooted) prototype objects, so they
    // stay reachable for the runtime's lifetime.
    const proto::ProtoString* nameKey =
        proto::ProtoString::createSymbol(ctx, "__class_name__");
    auto stamp = [&](const proto::ProtoObject* proto, const char* name) {
        const_cast<proto::ProtoObject*>(proto)->setAttribute(
            ctx, nameKey, ctx->fromUTF8String(name));
    };
    stamp(out.objectProto,       "Object");
    stamp(out.numberProto,       "Number");
    stamp(out.smallIntegerProto, "SmallInteger");
    stamp(out.largeIntegerProto, "LargeInteger");
    stamp(out.floatProto,        "Float");
    stamp(out.booleanProto,      "Boolean");
    stamp(out.stringProto,       "String");
    stamp(out.symbolProto,       "Symbol");
    stamp(out.blockProto,        "Block");
    stamp(out.actorProto,        "Actor");
    stamp(out.futureProto,       "Future");
    stamp(out.nilProto,          "UndefinedObject");
    stamp(out.exceptionProto,    "Exception");
    stamp(out.errorProto,        "Error");
    stamp(out.warningProto,      "Warning");

    // Track 1 slice 2 (EXC-a): the class-derived `resumable` marker. Carried
    // on the class prototypes so an instance inherits it via the chain;
    // `Error` overrides `Exception`'s true with false. EXC-a does not yet act
    // on this flag (resume:/retry/pass land in EXC-b) — it is recorded now so
    // the hierarchy is complete and EXC-b can read it without a re-bootstrap.
    const proto::ProtoString* resumableKey =
        proto::ProtoString::createSymbol(ctx, "__resumable__");
    auto markResumable = [&](const proto::ProtoObject* proto, bool resumable) {
        const_cast<proto::ProtoObject*>(proto)->setAttribute(
            ctx, resumableKey, resumable ? PROTO_TRUE : PROTO_FALSE);
    };
    markResumable(out.exceptionProto, true);   // Exception — resumable
    markResumable(out.errorProto,     false);  // Error — not resumable
    markResumable(out.warningProto,   true);   // Warning — resumable
}

} // namespace protoST
