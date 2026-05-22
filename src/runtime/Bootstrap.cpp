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
    // Atom prototype — a shared mutable cell with optimistic-concurrency CAS.
    out.atomProto         = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);

    // Track 1 slice 2 (EXC-a): exception class hierarchy. Ordinary mutable
    // prototypes — `Exception subclass: #MyError` works through the standard
    // `subclass:`/`newChild` path with no special-casing. `signal`, `on:do:`
    // and `return:` are bound later by installExceptionPrimitives.
    out.exceptionProto    = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);
    out.errorProto        = const_cast<proto::ProtoObject*>(out.exceptionProto)->newChild(ctx, /*isMutable=*/true);
    out.warningProto      = const_cast<proto::ProtoObject*>(out.exceptionProto)->newChild(ctx, /*isMutable=*/true);
    // MNT-b2 (D3 / D8): two runtime-signalled Error subclasses. They are
    // children of `Error`, so an `on: Error do:` guard catches them, and they
    // inherit `Error`'s non-resumable marker.
    out.messageNotUnderstoodProto =
        const_cast<proto::ProtoObject*>(out.errorProto)->newChild(ctx, /*isMutable=*/true);
    out.blockCannotReturnProto =
        const_cast<proto::ProtoObject*>(out.errorProto)->newChild(ctx, /*isMutable=*/true);

    // Track 2 slice a (COL-a): collection class hierarchy. Ordinary mutable
    // prototypes — the abstract `Collection` carries the derived iteration
    // protocol (collect:/select:/detect:/...), `Array` carries the concrete
    // base operations. `SequenceableCollection` and `HashedCollection` are
    // declared now even though only `Array` is concrete in COL-a, so the
    // hierarchy slot exists for OrderedCollection/Set/Bag/Dictionary (COL-b..e).
    out.collectionProto             = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);
    out.sequenceableCollectionProto = const_cast<proto::ProtoObject*>(out.collectionProto)->newChild(ctx, /*isMutable=*/true);
    out.hashedCollectionProto       = const_cast<proto::ProtoObject*>(out.collectionProto)->newChild(ctx, /*isMutable=*/true);
    out.arrayProto                  = const_cast<proto::ProtoObject*>(out.sequenceableCollectionProto)->newChild(ctx, /*isMutable=*/true);
    // Track 2 slice b (COL-b): `OrderedCollection` — a growable sequenceable
    // collection, ProtoList-backed like `Array` but with add:/remove* mutators.
    out.orderedCollectionProto      = const_cast<proto::ProtoObject*>(out.sequenceableCollectionProto)->newChild(ctx, /*isMutable=*/true);
    // Track 2 slice e (COL-e): `Interval` — a LAZY sequenceable collection.
    // Unlike `Array`/`OrderedCollection` it has no `__data__`; an instance
    // carries `start`/`stop`/`step` and computes its elements on demand.
    out.intervalProto               = const_cast<proto::ProtoObject*>(out.sequenceableCollectionProto)->newChild(ctx, /*isMutable=*/true);
    // Track 2 slice c (COL-c): `Set` and `Bag` — hashed collections. `Set` is
    // ProtoSet-backed (deduplicating), `Bag` is ProtoMultiset-backed (counting
    // duplicates). Both share the mutable-holder `__data__` representation.
    out.setProto                    = const_cast<proto::ProtoObject*>(out.hashedCollectionProto)->newChild(ctx, /*isMutable=*/true);
    out.bagProto                    = const_cast<proto::ProtoObject*>(out.hashedCollectionProto)->newChild(ctx, /*isMutable=*/true);
    // Track 2 slice d (COL-d): `Dictionary` — a key->value map with arbitrary
    // object keys, backed by a hash->bucket `ProtoSparseList`. `Association` is
    // a minimal key->value pair (a direct child of `Object`, NOT a collection)
    // supporting `associationsDo:` and the `aKey -> aValue` literal.
    out.dictionaryProto             = const_cast<proto::ProtoObject*>(out.hashedCollectionProto)->newChild(ctx, /*isMutable=*/true);
    out.associationProto            = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);

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
    stamp(out.atomProto,         "Atom");
    stamp(out.nilProto,          "UndefinedObject");
    stamp(out.exceptionProto,    "Exception");
    stamp(out.errorProto,        "Error");
    stamp(out.warningProto,      "Warning");
    stamp(out.messageNotUnderstoodProto, "MessageNotUnderstood");
    stamp(out.blockCannotReturnProto,    "BlockCannotReturn");
    stamp(out.collectionProto,             "Collection");
    stamp(out.sequenceableCollectionProto, "SequenceableCollection");
    stamp(out.hashedCollectionProto,       "HashedCollection");
    stamp(out.arrayProto,                  "Array");
    stamp(out.orderedCollectionProto,      "OrderedCollection");
    stamp(out.intervalProto,               "Interval");
    stamp(out.setProto,                    "Set");
    stamp(out.bagProto,                    "Bag");
    stamp(out.dictionaryProto,             "Dictionary");
    stamp(out.associationProto,            "Association");

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

    // Pre-intern the hot-path attribute vocabulary once, here, so the actor /
    // Future / Atom message paths never pay a per-operation SymbolTable
    // lookup. See Bootstrap::Symbols.
    auto S = [&](const char* n) { return proto::ProtoString::createSymbol(ctx, n); };
    out.sym.mailbox         = S("__mailbox__");
    out.sym.wrapped         = S("__wrapped__");
    out.sym.selector        = S("__selector__");
    out.sym.args            = S("__args__");
    out.sym.future          = S("__future__");
    out.sym.state           = S("__state__");
    out.sym.value           = S("__value__");
    out.sym.error           = S("__error__");
    out.sym.thenCbs         = S("__then_cbs__");
    out.sym.catchCbs        = S("__catch_cbs__");
    out.sym.waiters         = S("__waiters__");
    out.sym.settling        = S("__settling__");
    out.sym.suspendedFrame  = S("__suspended_frame__");
    out.sym.waitingOn       = S("__waiting_on__");
    out.sym.suspendedFuture = S("__suspended_future__");
    out.sym.bcPtr           = S("__bc_ptr__");
    out.sym.captured        = S("__captured__");
    out.sym.atomValue       = S("__atom_value__");
}

} // namespace protoST
