#pragma once

// Forward declarations to avoid pulling protoCore headers everywhere.
namespace proto {
    class ProtoSpace;
    class ProtoContext;
    class ProtoObject;
    class ProtoString;
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
    // Atom: a shared mutable cell with lock-free optimistic-concurrency
    // compare-and-swap (the agent/atom pair, atom side).
    const proto::ProtoObject* atomProto          = nullptr;
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

    // Pre-interned hot-path attribute symbols.
    //
    // Interning is idempotent — the same content always yields the same
    // pointer within a ProtoSpace — so a symbol, once interned, is a stable
    // value that can be cached. These are interned ONCE at bootstrap and the
    // pointers cached here, per-runtime (which is per-ProtoSpace, so this is
    // free of the cross-space dangling that a function-local `static` has —
    // deviation D2). The actor / Future / Atom hot paths read these instead
    // of calling createSymbol — a SymbolTable hash+lock — on every message.
    struct Symbols {
        const proto::ProtoString* mailbox         = nullptr;  // __mailbox__
        const proto::ProtoString* wrapped         = nullptr;  // __wrapped__
        const proto::ProtoString* selector        = nullptr;  // __selector__
        const proto::ProtoString* args            = nullptr;  // __args__
        const proto::ProtoString* future          = nullptr;  // __future__
        const proto::ProtoString* state           = nullptr;  // __state__
        const proto::ProtoString* value           = nullptr;  // __value__
        const proto::ProtoString* error           = nullptr;  // __error__
        const proto::ProtoString* thenCbs         = nullptr;  // __then_cbs__
        const proto::ProtoString* catchCbs        = nullptr;  // __catch_cbs__
        const proto::ProtoString* waiters         = nullptr;  // __waiters__
        const proto::ProtoString* settling        = nullptr;  // __settling__
        const proto::ProtoString* suspendedFrame  = nullptr;  // __suspended_frame__
        const proto::ProtoString* waitingOn       = nullptr;  // __waiting_on__
        const proto::ProtoString* suspendedFuture = nullptr;  // __suspended_future__
        const proto::ProtoString* bcPtr           = nullptr;  // __bc_ptr__
        const proto::ProtoString* captured        = nullptr;  // __captured__
        const proto::ProtoString* atomValue       = nullptr;  // __atom_value__
        const proto::ProtoString* ready           = nullptr;  // __ready__  (lock-free ready queue)
        const proto::ProtoString* sched           = nullptr;  // __sched__  (per-actor 3-state flag)
        const proto::ProtoString* anchored        = nullptr;  // __anchored__ (in the live registry?)
        const proto::ProtoString* homeFrame       = nullptr;  // __home_frame__ (block's home method id)
        const proto::ProtoString* blockSelf       = nullptr;  // __block_self__ (block's captured self)
        // Ready-queue rework (2026-05-23): GC-anchor pair for the intrusive
        // lock-free ReadyStack. `live` is the per-actor flag CAS'd from
        // FALSE/absent to TRUE on first enqueue; the CAS winner appends the
        // actor to the runtime-wide `liveActors` ProtoList rooted under
        // liveRegistry. See docs/superpowers/specs/2026-05-23-ready-queue-mpmc-spec.md.
        const proto::ProtoString* live            = nullptr;  // __live__       (per-actor anchor flag)
        const proto::ProtoString* liveActors      = nullptr;  // __live_actors__ (anchor list on liveRegistry)
        // F6 v5 (2026-05-23): single global task list (ProtoList of task
        // ProtoObjects) replaces the per-actor mailbox + global ready queue.
        // Each task has: __actor__, selector (sym.selector already cached),
        // args (sym.args), future (sym.future). Per-actor `__lockHandle__`
        // is an ExternalPointer to a C++ ActorLock (binary_semaphore-based
        // blocking lock) — enforces single-thread-of-execution per actor.
        // See docs/superpowers/specs/2026-05-23-task-list-spec.md (TODO).
        const proto::ProtoString* tasks           = nullptr;  // __tasks__ (on liveRegistry: the one task list)
        const proto::ProtoString* actor           = nullptr;  // __actor__ (on a task: which actor it targets)
        const proto::ProtoString* lockHandle      = nullptr;  // __lockHandle__ (on actor: ExtPtr to ActorLock)
        const proto::ProtoString* resume          = nullptr;  // __resume__ (on a task: marks a resume-from-yield task)
        // 2026-05-23 night: profile of saturation under 8 workers showed
        // 51 % of CPU in SymbolTable::intern + mutex contention, traced to
        // two createSymbol calls per SEND_* dispatch (one for __class_name__,
        // one for __class_side__ — both > 6 bytes, so they skip the
        // inline-string path and hit the shard mutex). Cache them here so
        // every SEND reads a Bootstrap-perpetual pointer instead.
        const proto::ProtoString* className       = nullptr;  // __class_name__ (every SEND class-side filter)
        const proto::ProtoString* classSide       = nullptr;  // __class_side__ (every SEND class-side filter)
    } sym;
};

// Build the prototype tree on top of `sp.objectPrototype` and bind the result
// into the relevant protoCore primitive slots so that literals dispatch
// through the Smalltalk chain.
void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out);

} // namespace protoST
