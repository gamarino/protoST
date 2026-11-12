#pragma once

#include <string>

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
    const proto::ProtoObject* integerProto       = nullptr;
    const proto::ProtoObject* characterProto     = nullptr;   // protoCore's unicode char prototype   // Integer: Number > Integer > Small/Large
    const proto::ProtoObject* smallIntegerProto  = nullptr;
    const proto::ProtoObject* largeIntegerProto  = nullptr;
    const proto::ProtoObject* floatProto         = nullptr;
    const proto::ProtoObject* booleanProto       = nullptr;
    const proto::ProtoObject* stringProto        = nullptr;
    const proto::ProtoObject* symbolProto        = nullptr;
    // ByteArray: an immutable value over protoCore's ProtoByteBuffer
    // (lib/kernel/bytes.st, src/primitives/bytes_prims.cpp). Like String it
    // is a direct child of Object, and a ProtoByteBuffer value dispatches to
    // it: through the space's bufferPrototype slot in a space protoST owns,
    // through methodHolder otherwise.
    const proto::ProtoObject* byteArrayProto     = nullptr;
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
    const proto::ProtoObject* arithmeticErrorProto = nullptr; // ArithmeticError < Error (resumable)
    const proto::ProtoObject* zeroDivideProto    = nullptr;   // ZeroDivide < ArithmeticError
    const proto::ProtoObject* messageProto       = nullptr;   // Message (selector, arguments)
    const proto::ProtoObject* blockCannotReturnProto    = nullptr;
    // NonBooleanReceiver < Error, resumable: a loop condition that is not a
    // Boolean (loopConditionIsTrue).
    const proto::ProtoObject* nonBooleanReceiverProto   = nullptr;
    // The prototype of every forward that DEOPT_ENTER leaves in a frame slot
    // whose variable a de-optimised closure shares (ExecutionEngine.cpp,
    // "De-optimisation of inlined value conditionals"). A slot value is a
    // forward exactly when this is its first parent: no Smalltalk program can
    // reach it, so no program value is ever mistaken for one. Not a class.
    const proto::ProtoObject* deoptForwardProto  = nullptr;
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
    //     |-- Set                    (concrete — ProtoMap via the hashed-collection helper)
    //     `-- Bag                    (concrete — ProtoList, one slot per occurrence)
    const proto::ProtoObject* setProto                    = nullptr;
    const proto::ProtoObject* bagProto                    = nullptr;
    // Track 2 slice d (COL-d): the key->value map.
    //   HashedCollection
    //     `-- Dictionary             (concrete — hash->bucket ProtoSparseList)
    // `Association` is a minimal key->value pair (child of Object), not a
    // collection — it supports `associationsDo:` and the `->` literal.
    const proto::ProtoObject* dictionaryProto             = nullptr;
    const proto::ProtoObject* associationProto            = nullptr;

    // Call-form named-arg "unset" sentinel. The SEND_CALL dispatcher writes
    // this singleton into named-arg slots that the caller omitted; the
    // method-decl prologue checks each slot against it and evaluates the
    // declared default when matched. Distinct from `nil` so users can pass
    // `nil` as an explicit named-arg value. Created once at bootstrap and
    // perpetually pinned (child of objectProto, mirroring the pattern used
    // for the exception-hierarchy roots).
    const proto::ProtoObject* unsetMarker                 = nullptr;

    // K14, D52: the class protocol. `Behavior` (a child of Object) holds what
    // every class and metaclass understands; `Class` and `Metaclass` are its
    // children. `Object class`, the root of the metaclass chain, is a child
    // of `Class`, so a class object reaches Behavior's protocol and then
    // Object's (src/runtime/Bootstrap.h, ensureMetaclass).
    const proto::ProtoObject* behaviorProto               = nullptr;
    const proto::ProtoObject* classProto                  = nullptr;
    const proto::ProtoObject* metaclassProto              = nullptr;

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
        // The unprocessed tail of the batch the current turn took out of the
        // mailbox queue. ProtoMPSCQueue::takeAll hands over every queued
        // message at once, but a turn can end before it has run them all (a
        // FutureYield parks the actor); the remainder is parked here, an
        // attribute of the actor, so that it stays reachable for the garbage
        // collector and is consumed — in FIFO order, ahead of the queue —
        // when the actor next runs. Absent or nil means there is none.
        const proto::ProtoString* pending         = nullptr;  // __pending__
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
        const proto::ProtoString* targetActor     = nullptr;  // __target_actor__
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
        // liveRegistry. See docs/archive/design-specs/2026-05-23-ready-queue-mpmc-spec.md.
        const proto::ProtoString* live            = nullptr;  // __live__       (per-actor anchor flag)
        const proto::ProtoString* liveActors      = nullptr;  // __live_actors__ (anchor list on liveRegistry)
        // F6 v5 (2026-05-23): single global task list (ProtoList of task
        // ProtoObjects) replaces the per-actor mailbox + global ready queue.
        // Each task has: __actor__, selector (sym.selector already cached),
        // args (sym.args), future (sym.future). No separate design document
        // was written for it.
        const proto::ProtoString* tasks           = nullptr;  // __tasks__ (on liveRegistry: the one task list)
        const proto::ProtoString* actor           = nullptr;  // __actor__ (on a task: which actor it targets)
        const proto::ProtoString* resume          = nullptr;  // __resume__ (on a task: marks a resume-from-yield task)
        // protoST 0.9.0 timers (TimerService.h): `timers` holds, on
        // liveRegistry, the ProtoSparseList (entry id -> Future) that roots
        // every pending timer; `timeout` marks a timer task, TRUE to reject
        // its __future__ with a TimeoutError, FALSE to resolve it with nil.
        const proto::ProtoString* timers          = nullptr;  // __timers__ (on liveRegistry: pending timers)
        const proto::ProtoString* timeout         = nullptr;  // __timeout__ (on a task: marks a timer settle)
        const proto::ProtoString* ms              = nullptr;  // __ms__ (on a timer task: the delay asked for)
        const proto::ProtoString* timerId         = nullptr;  // __timer_id__ (on a Future: its pending timer entry)
        const proto::ProtoString* timedOutAfter   = nullptr;  // __timedOutAfter: (kernel selector sent on a timeout)
        // 2026-05-23 night: profile of saturation under 8 workers showed
        // 51 % of CPU in SymbolTable::intern + mutex contention, traced to
        // two createSymbol calls per SEND_* dispatch (one for __class_name__,
        // one for the class-side marker of the time — both > 6 bytes, so they skip the
        // inline-string path and hit the shard mutex). Cache them here so
        // every SEND reads a Bootstrap-perpetual pointer instead.
        const proto::ProtoString* className       = nullptr;  // __class_name__ (every SEND class-side filter)
        const proto::ProtoString* classId         = nullptr;  // __class_id__ (super: the defining class)
        const proto::ProtoString* metaclass       = nullptr;  // __metaclass__ (methodHolder of a class)
        const proto::ProtoString* soleInstance    = nullptr;  // __sole_instance__ (a metaclass's class)
        // 2026-06-15: actor priority bands (high/medium/low). `__priority__`
        // is a SmallInteger 0/1/2 stored on the actor; absence means medium.
        // The scheduler reads this on schedule() to pick which of three
        // ReadyStacks to push onto; workers drain highest non-empty band
        // first. See docs/tutorial/10-actors-and-futures.md §10.11.
        const proto::ProtoString* priority        = nullptr;  // __priority__   (on actor: 0=high, 1=medium, 2=low)
        // The backing ProtoList of an Array or OrderedCollection, or the
        // ProtoMap of a Set or Dictionary.
        const proto::ProtoString* data            = nullptr;  // __data__
        // The storage keys of the other built-in collections. The collection
        // primitives read them on every call, so they are interned once here
        // rather than on each use (0.10.0).
        const proto::ProtoString* size            = nullptr;  // __size__ (Set, Dictionary entry count)
        const proto::ProtoString* intervalStart   = nullptr;  // __interval_start__
        const proto::ProtoString* intervalStop    = nullptr;  // __interval_stop__
        const proto::ProtoString* intervalStep    = nullptr;  // __interval_step__
        const proto::ProtoString* assocKey        = nullptr;  // __assoc_key__
        const proto::ProtoString* assocValue      = nullptr;  // __assoc_value__
        // The two attributes of a forward (deoptForwardProto): the scope
        // dictionary that now holds the variable and the variable's name.
        const proto::ProtoString* deoptDict       = nullptr;  // __deopt_dict__
        const proto::ProtoString* deoptName       = nullptr;  // __deopt_name__
        // Selectors the primitives send on every call of some paths (hashed
        // collections, sort, instance creation, iteration of a user
        // collection), interned once (0.10.0).
        const proto::ProtoString* equal           = nullptr;  // =
        const proto::ProtoString* hash            = nullptr;  // hash
        const proto::ProtoString* lessOrEqual     = nullptr;  // <=
        const proto::ProtoString* initialize      = nullptr;  // initialize
        const proto::ProtoString* elementsForIteration = nullptr;  // __elementsForIteration
    } sym;
};

// Build the prototype tree on top of `sp.objectPrototype`.  When
// `bindSpaceSlots` (protoST owns the space) the result is also bound into
// the space's primitive prototype slots, so that literals dispatch through the
// Smalltalk chain for any code of the space.  A runtime that runs in another
// runtime's space (STRuntime(proto::ProtoSpace&)) leaves the slots to their
// owner and resolves its own sends through methodHolder.
void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out,
                         bool bindSpaceSlots = true);

// Where a protoST send to `recv` starts its method lookup: protoST's own
// prototype for a primitive value (nil, a Boolean, an Integer, a Float, a
// Character, a String or Symbol), the metaclass for a class, `Metaclass` for
// a metaclass (both: the object `recv` owns as `__metaclass__`), and `recv`
// itself for anything else.  In a
// space protoST owns this is exactly what protoCore's lookup through the
// space's primitive slots finds; in another runtime's space, whose slots hold
// that runtime's prototypes, it keeps protoST's semantics for protoST code.
const proto::ProtoObject* methodHolder(const Bootstrap& b, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* recv);

// True when `v` is a Character (a protoCore unicode char).
bool isCharacterValue(proto::ProtoContext* ctx, const proto::ProtoObject* v);

// --- Classes and metaclasses (K14, D52) ----------------------------------------
//
// A class object is the prototype of its instances, so its own attributes are
// its instance-side methods. Every class also has a metaclass object (`Foo
// class`), which holds its class-side methods and is the method holder of the
// class object itself: a send to a class looks the selector up on the
// metaclass chain
//
//     Foo class -> Super class -> ... -> Object class -> Class -> Behavior -> Object
//
// and never on the class's own instance side, as in Smalltalk-80 and Pharo.
// `Behavior` holds the protocol every class understands (`new`, `name`,
// `selectors`, `subclass:` ...), `Class` the protocol of classes, and
// `Metaclass` that of metaclasses: a metaclass's method holder is `Metaclass`
// (`Foo class class == Metaclass`). Both sides of a class are ordinary
// prototype chains, so inheritance, `super`, mixins (`uses:`) and
// `addBehavior:` treat them alike.
//
// A class (or metaclass) owns `__metaclass__`, the object that holds the
// methods it understands; methodHolder reads it, and an object without it
// (every instance) is its own method holder. A metaclass owns
// `__sole_instance__`, its class.

// The metaclass of `cls` (an object that owns `__class_name__`), created on
// first use: a child of the metaclass of `cls`'s superclass (of every class
// on its chain, for a class with mixins), `Class` for a root class.
const proto::ProtoObject* ensureMetaclass(const Bootstrap& b, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* cls);

// True when `obj` is a metaclass (it owns `__sole_instance__`).
bool isMetaclass(const Bootstrap& b, proto::ProtoContext* ctx, const proto::ProtoObject* obj);

} // namespace protoST
