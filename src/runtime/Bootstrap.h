#pragma once

// Forward declarations to avoid pulling protoCore headers everywhere.
namespace proto {
    class ProtoSpace;
    class ProtoContext;
    class ProtoObject;
}

namespace protoST {

// Minimal Smalltalk-flavoured prototype hierarchy.
//
// For F2 we hand-roll the base prototypes from C++ so that literal values
// produced by the runtime have a stable parent chain.  Primitive methods
// (#+, #=, #printString, ...) attach in Tasks 40-43.
//
//     Object
//       |-- Number
//       |     |-- SmallInteger
//       |     |-- LargeInteger
//       |     `-- Float
//       |-- Boolean
//       |-- String
//       |     `-- Symbol
//       |-- Block
//       `-- UndefinedObject (nil)
//
// Each pointer is owned by the ProtoSpace; we only hold non-owning views.
struct Bootstrap {
    const proto::ProtoObject* objectProto        = nullptr;
    const proto::ProtoObject* numberProto        = nullptr;
    const proto::ProtoObject* smallIntegerProto  = nullptr;
    const proto::ProtoObject* largeIntegerProto  = nullptr;
    const proto::ProtoObject* floatProto         = nullptr;
    const proto::ProtoObject* booleanProto       = nullptr;
    const proto::ProtoObject* stringProto        = nullptr;
    const proto::ProtoObject* symbolProto        = nullptr;
    const proto::ProtoObject* blockProto         = nullptr;
    // F6: actor model — mutable so methods can later be bound on them.
    const proto::ProtoObject* actorProto         = nullptr;
    const proto::ProtoObject* futureProto        = nullptr;
    const proto::ProtoObject* nilProto           = nullptr;
    // Track 1 slice 2 (EXC-a): exception class hierarchy.
    //   Exception
    //     |-- Error
    //     `-- Warning
    // Mutable so signal / on:do: / return: primitives can be bound on them.
    const proto::ProtoObject* exceptionProto     = nullptr;
    const proto::ProtoObject* errorProto         = nullptr;
    const proto::ProtoObject* warningProto       = nullptr;
    // MNT-b2 (D3 / D8): two concrete Error subclasses signalled by the runtime
    // itself rather than by a script-level `signal`. `MessageNotUnderstood` is
    // raised when a send resolves no method (an unknown selector);
    // `BlockCannotReturn` is raised when a `^` runs in a block whose home
    // method has already returned. Both are children of `Error`, so both are
    // caught by an ordinary `on: Error do:` guard and are non-resumable.
    const proto::ProtoObject* messageNotUnderstoodProto = nullptr;
    const proto::ProtoObject* blockCannotReturnProto    = nullptr;
    // Track 2 slice a (COL-a): collection class hierarchy.
    //   Collection                 (abstract — shared iteration protocol)
    //     |-- SequenceableCollection (abstract — ordered, indexable)
    //     |     |-- Array            (concrete — ProtoList backing)
    //     |     `-- OrderedCollection (concrete — ProtoList backing, growable)
    //     `-- HashedCollection      (abstract — Set/Bag/Dictionary later)
    // Mutable so the collection primitives can be bound on them.
    const proto::ProtoObject* collectionProto             = nullptr;
    const proto::ProtoObject* sequenceableCollectionProto = nullptr;
    const proto::ProtoObject* hashedCollectionProto       = nullptr;
    const proto::ProtoObject* arrayProto                  = nullptr;
    // Track 2 slice b (COL-b): growable sequenceable collection.
    const proto::ProtoObject* orderedCollectionProto      = nullptr;
    // Track 2 slice e (COL-e): `Interval` — a lazy sequenceable collection.
    //   SequenceableCollection
    //     `-- Interval               (lazy — no backing store; start/stop/step)
    // An Interval (`1 to: 10 [by: 2]`) computes its elements on demand from the
    // three bound attributes `start`/`stop`/`step` — it carries no `__data__`.
    const proto::ProtoObject* intervalProto               = nullptr;
    // Track 2 slice c (COL-c): hashed collections.
    //   HashedCollection
    //     |-- Set                    (concrete — ProtoSet backing)
    //     `-- Bag                    (concrete — ProtoMultiset backing)
    const proto::ProtoObject* setProto                    = nullptr;
    const proto::ProtoObject* bagProto                    = nullptr;
    // Track 2 slice d (COL-d): the key->value map.
    //   HashedCollection
    //     `-- Dictionary             (concrete — hash->bucket ProtoSparseList)
    // `Association` is a minimal key->value pair (child of Object), not a
    // collection — it supports `associationsDo:` and the `->` literal.
    const proto::ProtoObject* dictionaryProto             = nullptr;
    const proto::ProtoObject* associationProto            = nullptr;
};

// Build the prototype tree on top of `sp.objectPrototype` and bind the result
// into the relevant protoCore primitive slots so that literals dispatch
// through the Smalltalk chain.
void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out);

} // namespace protoST
