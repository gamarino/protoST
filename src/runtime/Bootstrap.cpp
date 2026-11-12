#include "Bootstrap.h"
#include "protoCore.h"
#include "runtime/ListIndex.h"
#include "runtime/TransientPin.h"

#include <string>
#include <vector>

#include <cstdint>

namespace protoST {

void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out,
                         bool bindSpaceSlots) {
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
    // Integer sits between Number and the two concrete integer classes, as in
    // Smalltalk-80, so `3 isKindOf: Integer` holds and Integer methods reach both.
    out.integerProto      = out.numberProto->newChild(ctx, /*isMutable=*/true);
    out.smallIntegerProto = out.integerProto->newChild(ctx, /*isMutable=*/true);
    out.largeIntegerProto = out.integerProto->newChild(ctx, /*isMutable=*/true);
    out.floatProto        = out.numberProto->newChild(ctx, /*isMutable=*/true);

    // Booleans, strings/symbols, blocks, nil.
    out.booleanProto      = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.stringProto       = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.symbolProto       = out.stringProto->newChild(ctx, /*isMutable=*/true);
    // ByteArray values are protoCore ProtoByteBuffers; like strings, they are
    // immutable values and their class sits directly under Object.
    out.byteArrayProto    = out.objectProto->newChild(ctx, /*isMutable=*/true);
    // Characters are protoCore's embedded unicode-char values; their
    // prototype is Character, so `$a class == Character`.
    out.characterProto    = out.objectProto->newChild(ctx, /*isMutable=*/true);
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
    // ArithmeticError sits between Error and ZeroDivide and is resumable, as
    // in Pharo: a handler may answer a value for the failed operation.
    out.arithmeticErrorProto =
        const_cast<proto::ProtoObject*>(out.errorProto)->newChild(ctx, /*isMutable=*/true);
    out.zeroDivideProto =
        const_cast<proto::ProtoObject*>(out.arithmeticErrorProto)->newChild(ctx, /*isMutable=*/true);
    out.messageProto    = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);
    out.blockCannotReturnProto =
        const_cast<proto::ProtoObject*>(out.errorProto)->newChild(ctx, /*isMutable=*/true);
    out.nonBooleanReceiverProto =
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
    // Track 2 slice c (COL-c): `Set` and `Bag` — hashed collections. `Set`
    // (deduplicating) keeps a ProtoMap through protoCore's hashed-collection
    // helper; `Bag` (counting duplicates) keeps a ProtoList with one slot per
    // occurrence (see collection_prims.cpp). Both share the mutable-holder
    // `__data__` representation.
    out.setProto                    = const_cast<proto::ProtoObject*>(out.hashedCollectionProto)->newChild(ctx, /*isMutable=*/true);
    out.bagProto                    = const_cast<proto::ProtoObject*>(out.hashedCollectionProto)->newChild(ctx, /*isMutable=*/true);
    // Track 2 slice d (COL-d): `Dictionary` — a key->value map with arbitrary
    // object keys, backed by a hash->bucket `ProtoSparseList`. `Association` is
    // a minimal key->value pair (a direct child of `Object`, NOT a collection)
    // supporting `associationsDo:` and the `aKey -> aValue` literal.
    out.dictionaryProto             = const_cast<proto::ProtoObject*>(out.hashedCollectionProto)->newChild(ctx, /*isMutable=*/true);
    out.associationProto            = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);

    // Call-form unset sentinel. A unique mutable child of objectProto so the
    // dispatcher can identify it by pointer identity (`==` in protoST is
    // identity equality). Pinned for the runtime's lifetime via the same
    // mechanism as the other bootstrap prototypes — it is reachable from
    // the Bootstrap struct held by the runtime.
    out.unsetMarker = const_cast<proto::ProtoObject*>(out.objectProto)->newChild(ctx, /*isMutable=*/true);

    // K14, D52: the class protocol (Bootstrap.h, "Classes and metaclasses").
    out.behaviorProto  = out.objectProto->newChild(ctx, /*isMutable=*/true);
    out.classProto     = out.behaviorProto->newChild(ctx, /*isMutable=*/true);
    out.metaclassProto = out.behaviorProto->newChild(ctx, /*isMutable=*/true);

    // Bind protoCore primitive slots so values produced by fromLong/fromDouble/etc.
    // walk up through our Smalltalk prototypes.  This mirrors protoJS's
    // NumberPrototype.cpp pattern (space->smallIntegerPrototype = const_cast<...>).
    //
    // Booleans (PROTO_TRUE/PROTO_FALSE) and PROTO_NONE remain bound to the
    // protoCore-built-in prototypes; once we attach our own primitives we will
    // re-point them.  For F2 the bare prototypes suffice.
    // Only in a space protoST owns: another runtime's space keeps its owner's
    // slots, and protoST's sends resolve through methodHolder instead.
    if (bindSpaceSlots) {
        sp.smallIntegerPrototype = const_cast<proto::ProtoObject*>(out.smallIntegerProto);
        sp.largeIntegerPrototype = const_cast<proto::ProtoObject*>(out.largeIntegerProto);
        sp.unicodeCharPrototype  = const_cast<proto::ProtoObject*>(out.characterProto);
        sp.floatPrototype        = const_cast<proto::ProtoObject*>(out.floatProto);
        sp.doublePrototype       = const_cast<proto::ProtoObject*>(out.floatProto);
        sp.stringPrototype       = const_cast<proto::ProtoObject*>(out.stringProto);
        // protoCore resolves every ProtoByteBuffer's prototype through this
        // slot (it is used for byte buffers only), so `#[1 2] class` is
        // ByteArray.
        sp.bufferPrototype       = const_cast<proto::ProtoObject*>(out.byteArrayProto);
        sp.booleanPrototype      = const_cast<proto::ProtoObject*>(out.booleanProto);
        // Route nil (PROTO_NONE) through our nilProto so that selectors defined
        // on objectProto (e.g. Object>>halt, Object>>printNl) are reachable from
        // the nil receiver. nilProto's parent is objectProto so the walk succeeds.
        sp.nonePrototype         = const_cast<proto::ProtoObject*>(out.nilProto);
    }

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
    stamp(out.integerProto,      "Integer");
    stamp(out.smallIntegerProto, "SmallInteger");
    stamp(out.largeIntegerProto, "LargeInteger");
    stamp(out.floatProto,        "Float");
    stamp(out.booleanProto,      "Boolean");
    stamp(out.stringProto,       "String");
    stamp(out.symbolProto,       "Symbol");
    stamp(out.byteArrayProto,    "ByteArray");
    stamp(out.characterProto,    "Character");
    stamp(out.blockProto,        "BlockClosure");
    stamp(out.actorProto,        "Actor");
    stamp(out.futureProto,       "Future");
    stamp(out.atomProto,         "Atom");
    stamp(out.nilProto,          "UndefinedObject");
    stamp(out.exceptionProto,    "Exception");
    stamp(out.errorProto,        "Error");
    stamp(out.warningProto,      "Warning");
    stamp(out.messageNotUnderstoodProto, "MessageNotUnderstood");
    stamp(out.arithmeticErrorProto,      "ArithmeticError");
    stamp(out.zeroDivideProto,           "ZeroDivide");
    stamp(out.messageProto,              "Message");
    stamp(out.blockCannotReturnProto,    "BlockCannotReturn");
    stamp(out.nonBooleanReceiverProto,   "NonBooleanReceiver");
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
    stamp(out.unsetMarker,                 "UnsetMarker");
    stamp(out.behaviorProto,               "Behavior");
    stamp(out.classProto,                  "Class");
    stamp(out.metaclassProto,              "Metaclass");

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
    // As in Pharo, a MessageNotUnderstood may be resumed with a value that
    // becomes the send's result.
    markResumable(out.messageNotUnderstoodProto, true);
    markResumable(out.arithmeticErrorProto, true);
    // As in Pharo, a NonBooleanReceiver is resumed with the truth to use.
    markResumable(out.nonBooleanReceiverProto, true);
    // Unhandled, every Error ends the activation -- a resumable one such as
    // MessageNotUnderstood included -- while an unhandled Warning or plain
    // Exception resumes with nil. The class decides, not resumability.
    const_cast<proto::ProtoObject*>(out.errorProto)->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "__unhandled_is_error__"), PROTO_TRUE);

    // Pre-intern the hot-path attribute vocabulary once, here, so the actor /
    // Future / Atom message paths never pay a per-operation SymbolTable
    // lookup. See Bootstrap::Symbols.
    auto S = [&](const char* n) { return proto::ProtoString::createSymbol(ctx, n); };
    out.sym.mailbox         = S("__mailbox__");
    out.sym.pending         = S("__pending__");
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
    out.sym.targetActor     = S("__target_actor__");
    out.sym.suspendedFuture = S("__suspended_future__");
    out.sym.bcPtr           = S("__bc_ptr__");
    out.sym.captured        = S("__captured__");
    out.sym.atomValue       = S("__atom_value__");
    out.sym.ready           = S("__ready__");
    out.sym.sched           = S("__sched__");
    out.sym.anchored        = S("__anchored__");
    out.sym.homeFrame       = S("__home_frame__");
    out.sym.blockSelf       = S("__block_self__");
    out.sym.live            = S("__live__");
    out.sym.liveActors      = S("__live_actors__");
    out.sym.tasks           = S("__tasks__");
    out.sym.actor           = S("__actor__");
    out.sym.resume          = S("__resume__");
    out.sym.timers          = S("__timers__");
    out.sym.timeout         = S("__timeout__");
    out.sym.ms              = S("__ms__");
    out.sym.timerId         = S("__timer_id__");
    out.sym.timedOutAfter   = S("__timedOutAfter:");
    // 2026-05-23 night: SEND-path class-side filter hot keys. See header.
    out.sym.className       = S("__class_name__");
    out.sym.classId         = S("__class_id__");
    out.sym.metaclass       = S("__metaclass__");
    out.sym.soleInstance    = S("__sole_instance__");
    out.sym.priority        = S("__priority__");
    out.sym.data            = S("__data__");
    out.sym.size            = S("__size__");
    out.sym.intervalStart   = S("__interval_start__");
    out.sym.intervalStop    = S("__interval_stop__");
    out.sym.intervalStep    = S("__interval_step__");
    out.sym.assocKey        = S("__assoc_key__");
    out.sym.assocValue      = S("__assoc_value__");

    // K14, D52: every built-in class gets its metaclass now, so the class-side
    // primitives (Array new:, Float pi, ...) can be bound on it. The unset
    // marker is a sentinel, not a class.
    for (const proto::ProtoObject* cls : {
             out.objectProto, out.numberProto, out.integerProto, out.smallIntegerProto,
             out.largeIntegerProto, out.floatProto, out.booleanProto, out.stringProto,
             out.symbolProto, out.byteArrayProto, out.characterProto, out.blockProto,
             out.actorProto, out.futureProto, out.atomProto, out.nilProto,
             out.exceptionProto, out.errorProto, out.warningProto,
             out.messageNotUnderstoodProto, out.arithmeticErrorProto, out.zeroDivideProto,
             out.messageProto, out.blockCannotReturnProto, out.nonBooleanReceiverProto,
             out.collectionProto,
             out.sequenceableCollectionProto, out.hashedCollectionProto, out.arrayProto,
             out.orderedCollectionProto, out.intervalProto, out.setProto, out.bagProto,
             out.dictionaryProto, out.associationProto, out.behaviorProto, out.classProto,
             out.metaclassProto})
        ensureMetaclass(out, ctx, cls);
}

bool isCharacterValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    // A unicode char is an embedded value whose prototype is the space's
    // unicode char slot (protoCore resolves it from the value's tag).
    if (!v || v == PROTO_NONE || v == PROTO_TRUE || v == PROTO_FALSE) return false;
    if (v->isInteger(ctx) || v->isFloat(ctx)) return false;
    return v->getPrototype(ctx) == ctx->space->unicodeCharPrototype;
}

bool isMetaclass(const Bootstrap& b, proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || (reinterpret_cast<uintptr_t>(obj) & 0x3F) != 0) return false;
    const proto::ProtoObject* s = obj->getOwnAttributeDirect(ctx, b.sym.soleInstance);
    return s && s != PROTO_NONE;
}

namespace {
bool ownsClassName(const Bootstrap& b, proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || (reinterpret_cast<uintptr_t>(obj) & 0x3F) != 0) return false;
    const proto::ProtoObject* n = obj->getOwnAttributeDirect(ctx, b.sym.className);
    return n && n != PROTO_NONE;
}
} // namespace

const proto::ProtoObject* ensureMetaclass(const Bootstrap& b, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* cls) {
    const proto::ProtoObject* meta = cls->getOwnAttributeDirect(ctx, b.sym.metaclass);
    if (meta && meta != PROTO_NONE) return meta;

    // The classes on cls's chain, nearest first: its superclass and its
    // superclass's ancestors, then each mixin subtree (getParents is the
    // flattened resolution order).
    std::vector<const proto::ProtoObject*> classes;
    if (const proto::ProtoList* chain = cls->getParents(ctx)) {
        TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
        const proto::proto_ulong n = chain->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n; ++i) {
            const proto::ProtoObject* p = chain->getAt(ctx, listIndex(i));
            if (ownsClassName(b, ctx, p) && !isMetaclass(b, ctx, p)) classes.push_back(p);
        }
    }

    const proto::ProtoObject* created = nullptr;
    if (classes.empty()) {
        // A root class (Object): its metaclass inherits Class.
        created = b.classProto->newChild(ctx, /*isMutable=*/true);
    } else {
        const proto::ProtoObject* superMeta = ensureMetaclass(b, ctx, classes.front());
        // Single inheritance: the superclass's chain holds every other class.
        std::size_t inherited = 0;
        if (const proto::ProtoList* sc = classes.front()->getParents(ctx)) {
            const proto::proto_ulong n = sc->getSize(ctx);
            for (proto::proto_ulong i = 0; i < n; ++i) {
                const proto::ProtoObject* p = sc->getAt(ctx, listIndex(i));
                if (ownsClassName(b, ctx, p) && !isMetaclass(b, ctx, p)) ++inherited;
            }
        }
        if (inherited + 1 == classes.size()) {
            created = superMeta->newChild(ctx, /*isMutable=*/true);
        } else {
            // Mixins: the metaclass chain mirrors the class chain, built as
            // makeSubclassWithMixins builds a class -- on an immutable shape,
            // adding the metaclasses last-wanted-first (addParent prepends a
            // parent with its ancestors and drops duplicates).
            const proto::ProtoObject* shape = b.classProto->newChild(ctx, /*isMutable=*/false);
            TransientPin pinShape(ctx, shape);
            for (std::size_t k = classes.size(); k > 0; --k) {
                const proto::ProtoObject* m = ensureMetaclass(b, ctx, classes[k - 1]);
                shape = shape->addParent(ctx, m);
                pinShape.reset(shape);
            }
            created = shape->newChild(ctx, /*isMutable=*/true);
        }
    }
    TransientPin pinMeta(ctx, created);
    std::string name = "Class";
    const proto::ProtoObject* own = cls->getOwnAttributeDirect(ctx, b.sym.className);
    if (own && own != PROTO_NONE)
        if (const proto::ProtoString* ns = own->asString(ctx)) name = ns->toStdString(ctx);
    auto* m = const_cast<proto::ProtoObject*>(created);
    m->setAttribute(ctx, b.sym.className, ctx->fromUTF8String((name + " class").c_str()));
    m->setAttribute(ctx, b.sym.soleInstance, cls);
    m->setAttribute(ctx, b.sym.metaclass, b.metaclassProto);
    const_cast<proto::ProtoObject*>(cls)->setAttribute(ctx, b.sym.metaclass, created);
    return created;
}

const proto::ProtoObject* methodHolder(const Bootstrap& b, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* recv) {
    if (!recv || recv == PROTO_NONE) return b.nilProto;
    if (recv == PROTO_TRUE || recv == PROTO_FALSE) return b.booleanProto;
    if (proto::isSmallInt(recv)) return b.smallIntegerProto;
    // An object cell (tag 0) is its own holder, unless it is a class or a
    // metaclass: then the object it owns as `__metaclass__` (D52).
    if ((reinterpret_cast<uintptr_t>(recv) & 0x3F) == 0) {
        const proto::ProtoObject* meta = recv->getOwnAttributeDirect(ctx, b.sym.metaclass);
        return (meta && meta != PROTO_NONE) ? meta : recv;
    }
    if (recv->isInteger(ctx)) return b.largeIntegerProto;
    if (recv->isFloat(ctx)) return b.floatProto;
    if (recv->asString(ctx)) return b.stringProto;
    if (isCharacterValue(ctx, recv)) return b.characterProto;
    if (recv->isByteBuffer(ctx)) return b.byteArrayProto;
    return recv;
}

} // namespace protoST

