# protoST: explicit state (value classes and instance-variable modifiers)

Status: **draft for review** (2026-11-23). Scope approved by the maintainer on
2026-11-19; every open question ruled on 2026-11-23 (§17). Author: Gustavo
Marino, with Claude.
This is a design spec only: nothing here is implemented yet.

Read first: LANGUAGE.md §4.4–§4.5 (instances, instance variables, runs of
assignments), §4.10 (methods installed late), §10.4 (helping waits), §10.6
(the synchronization boundary) and §10.8 (`Atom`, `setInstVar:from:to:`).

Bare section numbers (§5.3) refer to this spec; sections of the language
reference are written "LANGUAGE.md §n".

Markers used below:
- **(maintainer, 2026-11-19)**: part of the approved scope; binding.
- **(maintainer, 2026-11-23, Qn)**: a ruling on open question Qn of the first
  draft; binding. §17.1 lists them all.
- ***Proposed***: a decision this spec makes to fill a gap the scope left
  open. §17.2 lists them. §18 confirms that no question is open.

## 1. Intent

protoCore is immutable by default. A protoST object is a *mutable* protoCore
object: a reference that is republished, one immutable snapshot after another,
each time an instance variable is assigned (LANGUAGE.md §4.5). Today the
programmer cannot say which state is meant to change, which is meant never to
change, and which is shared between threads and must change only by
compare-and-swap. protoBaseST had to say it anyway. It does so by convention:
- a test (`tests/unit/values_immutable.st`) greps the sources for `:=` and
  `setInstVar:` and checks each hit against an allowlist
  (`tests/unit/_ivar_allowlist.txt`, 59 entries);
- private initialisers (`_setLabel:record:`) refuse a second call by hand;
- hand-written `_publish…:` methods (`_publishOid:`, `_publishSaved:`,
  `_publishMemo:`, `_publishOrderKey:`, `_publishKeySpec:`) fill a field once
  by `setInstVar: #f from: nil to: x`, and the loser of a race answers the
  winner's value.

This spec makes that state **explicit in the language**:
- **Value classes**, whose instances are protoCore *immutable* objects, born
  complete;
- three **instance-variable modifiers** on ordinary (mutable) classes:
  constant, write-once and atomic;
- a stated pattern for **invariants over several fields**: one immutable value
  held in one atomic variable;
- **locals unchanged**, with the reason written down.

The checks run where protoST already checks names: in the compiler when it can
see the class, when the method is installed when it cannot (LANGUAGE.md §4.10), and always
at run time. Late binding may move the moment an error is detected; it never
lets a write pass silently.

### 1.1 Why it matters (maintainer, 2026-11-19)

**One optimistic pattern, from the attribute to the database commit.**
- Read an immutable snapshot.
- Compute a new version with no side effects.
- Publish it only if the snapshot is still current.
- On conflict, run the computation again.

That is protoCore's attribute CAS. It is also protoBaseST's commit: the commit
signals `PBConflictError`, and `PBDatabase >> run:` runs the block again with
a fresh transaction. Taking this pattern all the way to the user, with one
vocabulary at every level, is the step this spec takes (§9).

**A direction: Croquet.** In David Reed's 1978 MIT thesis, *Naming and
Synchronization in a Decentralized Computer System* (pseudo-time), an object is
a sequence of immutable versions. That work is the precursor of TeaTime, the
replication protocol of Croquet. On protoCore, versions and snapshots come
almost for free. Deterministic replicated execution and a reflector that orders
messages remain real work. §19 presents this as direction, not as a promise.

### 1.2 Positioning

- protoST is a protoCore demonstrator. It is not a Smalltalk clone and not "a
  better Smalltalk". Value classes and modifiers exist because they show
  protoCore's model, not because Smalltalk lacks them.
- **Clarity beats familiar names.** The new forms name the immutable model
  (`valueSubclass:`, `withX:`, `update:with:`). Pharo's read-only objects
  (`beReadOnlyObject`) can be switched back to writable, so their names are not
  reused.
- The language stays late-bound (§3).
- Errors are C++ exceptions surfaced as Smalltalk exceptions. There is no
  pending-error flag, and a CAS retry is an ordinary loop.
- `self` is the object. A self-send dispatches directly, with no lock: no
  modifier adds one.

## 2. Goals, non-goals, anti-goals

**Goals**
- A value class whose instances cannot be changed by any path: the compiler,
  the interpreter, reflection and the CAS primitives all refuse.
- Mutable classes can mark a variable constant, write-once or atomic, and every
  write path honours the mark.
- protoBaseST can delete its allowlist convention and its hand-written `_set…:`
  initialisers, declare its write-once fields instead of publishing them by
  hand, and keep the same behaviour (§13).
- Value instances cost what protoCore immutable objects cost. They have no
  entry in the global mutable table, so a value is not a 5+k-cell mutable
  object but a 2+k-cell immutable one (the protoBaseST atom-weight
  measurements, 2026-11-19).
- The optimistic pattern of §9 is taught with the same words at every level.

**Non-goals**
- **STM**, or any transaction over several objects or several variables in the
  language (§8 gives the pattern instead).
- **Builder blocks** (`Point build: [:b | b x: 1. b y: 2]`).
- **Freeze**: a mutable phase followed by `freeze` / `beReadOnlyObject`, and
  thawing. protoCore offers `clone(ctx, false)` as freeze/thaw; protoST
  deliberately does not expose it for instances (§5.8).
- **Constant or single-assignment locals** (`let`, `const` temporaries): §4.
- **Deep immutability** checks on field values: immutability is shallow
  (maintainer, 2026-11-23, Q3; §5.6).
- A conditional multi-attribute write in protoCore. It is recorded as a
  possible future protoCore item, with no commitment (§20).

**Anti-goals**
- A value whose fields can be observed unset or half-set by any thread.
- A write that is refused silently. In particular, a CAS on an immutable
  receiver must not answer `false`: a retry loop would spin for ever.
- A lock, a mutex or a pending-exception flag anywhere in the new paths.
- A modifier that one write path forgets (reflection, `deepCopy`,
  `setInstVar:from:to:`, the debugger).

## 3. Constraints

- **No protoCore change.** Everything uses the public API of protoCore 3.6.1:
  `newChild(ctx, false)`, `setAttribute` and `setAttributes` (which answer a new
  object for an immutable receiver), `setAttributeIfEqual`, and
  `getOwnAttributeDirect`. Anything that would need protoCore is recorded in
  §20 (future protoCore items), with no commitment. The protoCore floor stays
  3.6.1.
- **Public API only.** Do not include `proto_internal.h`.
- **Late binding.** Modifiers and value-ness are recorded on the class object
  at run time, when the declaration runs. The compiler uses them when it can see
  the class; install-time resolution (LANGUAGE.md §4.10) uses them otherwise.
- **No new GC roots and no edits to the collector loop.** New transient objects
  are rooted the way protoST already roots them (operand stack, frame slots,
  `TransientPin`).
- **No `ProtoTuple` for fields.** Every tuple is interned forever (protoCore
  3.2+), so a value's fields must never be packed into one.
- ProtoThreads only, and no mutex held across a protoCore allocation (unchanged).
- The fast path of a plain instance-variable store (`STORE_INSTVAR`,
  `IVAR_GROUP`) gains no work. Every new check sits on a new opcode, at install
  time, or in reflection.

## 4. Locals stay as they are (maintainer, 2026-11-19)

Method and block temporaries and arguments do not change. Temporaries stay
automatic, rebindable variables, and arguments stay unassignable, as today
(`cannot assign to the argument 'x'`).

**Why (text for LANGUAGE.md):**
- A temporary belongs to one activation. No other thread can see it, so making
  it constant would buy no safety, only a second way to write the same code.
- The only way a temporary is shared is a block that captures it (LANGUAGE.md
  §6.3). When
  such a block crosses threads (an actor message argument, a `thenDo:`
  callback, a block run by another actor), the captured variable is shared
  storage. A write to it replaces the whole reference: it is never torn, but
  it is ordered with nothing. The guidance is to capture values, not
  variables, across threads, and to hand results back through a `Future`.
- Value classes and modifiers are about *object* state, which outlives
  activations and is shared. That is where explicit state pays.

## 5. Value classes

### 5.1 Declaration

```smalltalk
Object valueSubclass: #Point
  instanceVariableNames: 'x y'.

Object valueSubclass: #Rect
  instanceVariableNames: 'origin corner'
  derivedNames: 'area'.

Point valueSubclass: #Point3D
  instanceVariableNames: 'z'.
```

The grammar of LANGUAGE.md §3.2 gains one alternative:

```
classDecl  ::=  Identifier ('subclass:' | 'valueSubclass:') SymbolLit clause* '.'?
clause     ::=  'instanceVariableNames:' StringLit
             |  'classVariableNames:'    StringLit
             |  'uses:'                  Expression
             |  'derivedNames:'          StringLit      "valueSubclass: only"
             |  'constantNames:'         StringLit      "subclass: only (§6)"
             |  'writeOnceNames:'        StringLit      "subclass: only (§6)"
             |  'atomicNames:'           StringLit      "subclass: only (§6)"
             |  'package:' | 'category:' | 'poolDictionaries:' | 'tag:' …   "ignored, as today"
```

Clauses may come in any order, as today.
- The instance variables of a value class are its **fields**.
- `instanceVariableNames:` declares the fields the constructor receives.
- `derivedNames:` declares fields that `initialize` computes (§5.3). The
  constructor does not receive them.

**Message form.** Every class understands these, for a superclass reached
through an expression (LANGUAGE.md §4.10):
- `valueSubclass:`
- `valueSubclass:instanceVariableNames:`
- `valueSubclass:instanceVariableNames:derivedNames:`
- `valueSubclass:instanceVariableNames:uses:`

***Proposed:*** the compiler treats a top-level message-form statement whose
selector starts with `valueSubclass:` and whose class name is a literal symbol
as a value-class declaration, so it compiles that class's `initialize` in
pre-birth mode (§5.3). A value class the compiler cannot identify (a name bound
any other way) cannot have an `initialize`: installing one signals the error V9
of §12.

**Where a value class may sit in the hierarchy:**
- Its superclass is a value class, or a class that declares and inherits no
  instance variables (`Object`, a stateless abstract class). Anything else
  signals V1.
- Its `uses:` mixins, and the mixins `addBehavior:` adds later, declare and
  inherit no instance variables (V2).
- A value class can be subclassed only with `valueSubclass:`. `subclass:` on a
  value class signals V3, because a mutable "kind of `Point`" would break what
  every `Point` client relies on.
- `constantNames:`, `writeOnceNames:` and `atomicNames:` are refused on a value
  class (V4): every field of a value is fixed at birth.
- Class variables and class-instance variables are allowed. They are state of
  the class, not of its instances, and stay mutable.

### 5.2 The generated protocol

Declaring a value class installs these methods. All of them are installed when
the declaration runs (late binding), not by the compiler.

| Side | Selector | Meaning |
|---|---|---|
| class | **constructor**: the non-derived fields as keywords, inherited fields first (superclass chain from the root down), then own fields in declaration order: `Point x:y:`, `Point3D x:y:z:`, `Rect origin:corner:`. With no non-derived fields it is `new`. | Builds the value (§5.3). |
| instance | **`withX:`** for every non-derived field, own and inherited | A new value of the *receiver's* class, with that field replaced and every other non-derived field copied, built through the constructor, so `initialize` runs again (§5.4). |
| instance | **`withAll:`** (maintainer, 2026-11-23, Q2) | A new value of the receiver's class with several fields replaced at once, from `field -> value` pairs: one birth, one validation (§5.4). |
| instance | **readers** (`x`, `y`, `area`): one per field, derived included (maintainer, 2026-11-23, Q7) | Answers the field. |
| instance | **`=`** and **`hash`** (***Proposed***) | `=`: same class (`==`) and every non-derived field `=`. `hash`: combines the class and the non-derived fields' hashes, deterministic within a run. |

**Rules:**
- **The constructor, `withX:` and `withAll:` cannot be redefined.** Defining a
  method with one of these selectors on that class signals V8. They are the
  only ways a value is born, so they carry its invariants. A class adds more constructors under
  other names, and they call the generated one:
  `Point class >> origin ^ self x: 0 y: 0`.
- **Readers, `=` and `hash` can be overridden.** They take no part in birth.
- **Names:** `withX:` is `with` followed by the field name with its first
  letter uppercased (`x` gives `withX:`, `lowBound` gives `withLowBound:`); a
  leading underscore is kept (`_oid` gives `with_oid:`).
  - Two fields that generate the same selector signal V7. So does a field
    named `all`, whose `withAll:` would collide with the generated one.
  - A field named after a unary selector that `Object` understands (`class`,
    `hash`, `yourself`, `printString`, `name`…) signals V7, because its reader
    would hide `Object`'s method.
- **The constructor is bound to its class.** Sending an inherited generated
  constructor to a value subclass (`Point3D x: 1 y: 2`) signals V6: the
  subclass is built with its own constructor.
- **Other ways to make an instance are refused:** `new` on a value class with
  non-derived fields, `basicNew` and `newChild` all signal V5. So do the
  `Behavior` messages that make instances without the constructor.

### 5.3 Birth and `initialize` (maintainer, 2026-11-19)

**Construction ruling (maintainer, accepted 2026-11-19).** A value is born
complete, from the generated keyword constructor. `initialize` validates and
derives values **before birth**, from the received values. There is no
mutable-then-freeze phase, because a half-built `self` could escape. There are
no builder blocks.

**What the constructor does:**
1. It receives one value per non-derived field.
2. It binds the **pending fields**: one per field of the class, inherited
   included. Each received field holds its argument, and each derived field
   holds `nil`.
3. It runs the class's **pre-birth initializers**, from the root value class
   down to the receiver's class. Each initializer runs only if that class
   defines `initialize` (maintainer, 2026-11-23, Q1: automatic chaining, so a
   subclass can never skip its superclass's invariants).
4. It **gives birth**: one immutable protoCore object whose parent is the
   class, with every non-nil field set, made in one step. Nothing exists
   before this step.
5. It answers that object.

If an initializer signals, or exits through a non-local return, nothing is
born and the exception propagates. A non-local return from `initialize` itself
(`^ expr`) only ends that initializer; the value of `expr` is ignored.

**`initialize` in a value class is compiled in pre-birth mode:**
- **Each field is a temporary of the method,** preloaded with its pending
  value. It may be read, and **assigned** (to normalise a received value or to
  compute a derived one). This is the only place a field of a value class is
  ever assigned.
- A block in `initialize` captures fields exactly as it captures temporaries.
  If such a block outlives birth, it holds its own captured variables, and
  writing them changes nothing in the value.
- **`self` is a compile error** (V10): the instance does not exist yet. So is
  `super`, because chaining is automatic (Q1): the superclass's initializer
  has already run when the subclass's starts. Helpers are class-side methods
  reached by the class name (`Rect checkCorner: corner`).
- **A wait** (`aFuture wait`) behaves as a wait inside a block that a primitive
  evaluates (LANGUAGE.md §10.4): it blocks its worker, which helps. No other thread can
  observe anything, since nothing is born yet. The documentation advises
  against waiting in `initialize`.
- **Validation** signals an exception. The idiom is
  `^ InvalidArgument signal: '…'` (or an `Error` subclass of the program's
  own). `self error:` is not available: there is no `self`.

```smalltalk
Object valueSubclass: #Span
  instanceVariableNames: 'low high'.

Span >> initialize
  (low isNumber and: [high isNumber]) ifFalse: [
    ^ InvalidArgument signal: 'Span: low and high are numbers'].
  low > high ifTrue: [ | t | t := low. low := high. high := t ].   "normalise"

(Span low: 5 high: 2) low.          "=> 2"
(Span low: 1 high: 'x').           "signals InvalidArgument"

Object valueSubclass: #Rect
  instanceVariableNames: 'width height'
  derivedNames: 'area'.

Rect >> initialize
  area := width * height.

(Rect width: 3 height: 4) area.     "=> 12"
((Rect width: 3 height: 4) withWidth: 5) area.   "=> 20"
```

**`initialize` sent to a born value** signals V11. The method is listed by
reflection (`Span includesSelector: #initialize` is true), but it cannot run
on an instance.

### 5.4 Updates: `withX:` and `withAll:`

**`withX:`** changes one field.
- `p withX: 3` answers a new value. The receiver is unchanged.
- It is exactly the receiver's class's constructor, called with the receiver's
  non-derived fields and `x` replaced. So every pre-birth initializer runs again
  and the invariants hold for the new value.
- If the new field value is identical (`==`) to the current one, `withX:`
  answers the **receiver** and nothing is born (***Proposed***). The receiver
  already passed `initialize` with these exact fields.

**`withAll:`** changes several fields in one birth (maintainer, 2026-11-23,
Q2).

```smalltalk
Object valueSubclass: #Bounds
  instanceVariableNames: 'low high'.

Bounds >> initialize
  low <= high ifFalse: [
    ^ InvalidArgument signal: 'Bounds: low ' , low printString , ' > high ' , high printString].

b := Bounds low: 0 high: 3.
(b withLow: 5) withHigh: 10.                     "signals InvalidArgument: the intermediate 5..3"
(b withAll: { #low -> 5. #high -> 10 }) low.     "=> 5: one birth, validated once"
```

- **Why not chained `withX:`.** Each `withX:` is a birth, and each birth is
  validated. A chain passes through intermediate values, and an intermediate
  value can break a cross-field invariant that the final value satisfies, as
  above. Reordering the chain only moves the problem to other moves. So
  several fields that must change together change in one birth.
- **The argument** is a collection of `Association`s (`{ #low -> 5 }`, an
  `OrderedCollection`) or a `Dictionary`, whose associations are taken. Each
  key names a non-derived field, as a `Symbol` or a `String` (they are one
  thing for short names, D35). Each value is the field's new value.
- **Its meaning** is the receiver's class's constructor, called with the
  receiver's non-derived fields, each named field replaced. Every pre-birth
  initializer runs once, over the final values. The answer is an instance of
  the receiver's class, a subclass included.
- **Checks, all made before anything runs:**
  - a key that names no field of the receiver's class signals V14;
  - a key that names a derived field signals V15 (derived fields are computed
    by `initialize`, never given);
  - a field named twice signals V16, even with the same value;
  - an element that is not an `Association`, or an argument that is not a
    collection, signals V17.
- **Identity.** When every named field's new value is identical (`==`) to the
  current one, or the collection is empty, `withAll:` answers the receiver
  and nothing is born, as `withX:` does.

### 5.5 Compile-time and install-time rules

In any method of a value class, and in any block in it, assigning a field
signals V12, except in its `initialize`. That includes:
- methods installed late (LANGUAGE.md §4.10), where the error is reported when the method
  is installed;
- methods added to the value class from another module.

There is no write-group analysis (LANGUAGE.md §4.5) for value classes, because nothing is
assigned. Reads compile as today (`PUSH_INSTVAR` / `PUSH_OWN_INSTVAR`).

### 5.6 Run-time semantics

- A value is an immutable protoCore object. `setAttribute` on it answers a new
  object, and protoST refuses such a write instead of losing it.
  - **The store opcode.** `STORE_INSTVAR` already refuses this today with a
    `NativeError`. It now signals **`ModificationForbidden`** (V13), so that
    `on: ModificationForbidden do:` catches it.
  - **CAS primitives.** `compareAndSet:from:to:` and `update:with:` on a value
    signal `ModificationForbidden` (`setInstVar:from:to:` is removed in the
    release that brings values, §13.1). They **never
    answer `false`**, because a retry loop would spin for ever on a write that
    can never succeed.
  - **Reflection.** `instVarNamed:put:` and `instVarAt:put:` signal
    `ModificationForbidden` too (§11).
- Identity is kept. Two constructions with equal fields are two objects: `=` is
  true and `==` is false. Values are not interned.
- **Immutability is shallow** (maintainer, 2026-11-23, Q3). The binding of a
  field never changes, but a field may refer to a mutable object (an
  `OrderedCollection`, a mutable instance), and that object may change. The
  constructor does not inspect field values. LANGUAGE.md states the
  consequences:
  - the generated `=` and `hash` read the fields' own `=` and `hash`, so a
    value whose field holds a mutable object can change its `=` and `hash`;
    such a value should not be a `Dictionary` key or a `Set` element;
  - a value is safe to share between threads exactly as far as what its
    fields refer to is;
  - a value whose fields hold only values, immutable kernel objects
    (numbers, `String`, `ByteArray`, blocks) or actor references is
    immutable all the way down. That is the recommended form, not a checked
    one.
- **Shape.** All values of a class share one protoCore `ProtoShape` (protoCore
  3.6.1 shape transitions). The class keeps an immutable instance template,
  and birth is one `setAttributes` on it.
- **Printing** is unchanged: `a Point`, overridable through `printOn:`.
- **Actors.** A value may be sent to, held by, and read from any actor or
  thread without a proxy or a lock. `aValue asActor` is allowed but pointless:
  there is no state to serialise.

### 5.7 Inheritance and composition

- The fields of a value subclass are its superclass's fields followed by its
  own. Pre-birth initializers run from the root down (§5.3).
- A superclass's `withX:` sent to a subclass instance answers an instance of
  the subclass (§5.4).
- `addBehavior:` works as today (LANGUAGE.md §4.12, D21) with mixins that have
  no instance variables. It rebuilds the class, and the generated methods are
  installed on the rebuilt class.
- `isKindOf:`, `inheritsFrom:` and method lookup are unchanged.

### 5.8 Copying, and why there is no freeze

- `copy` and `shallowCopy` answer the receiver. A value never changes, so a copy
  is indistinguishable from it, as with `String` and numbers.
- `deepCopy` answers a value of the same class built through the constructor
  from the `deepCopy` of each non-derived field (***Proposed***). Its
  initializers run.
- **No freeze.** protoCore's `clone(ctx, false)` turns a mutable object into an
  immutable copy. protoST does not offer it for instances, for two reasons:
  - the copy is a different object, so references to the mutable original
    remain mutable;
  - the mutable phase before the freeze is where a half-built `self` escapes
    to another thread, a callback or a registry.

  Birth from the constructor has neither problem.

## 6. Instance-variable modifiers on mutable classes (maintainer, 2026-11-19)

### 6.1 Declaration

```smalltalk
Object subclass: #Account
  instanceVariableNames: 'history'
  constantNames: 'number owner'
  writeOnceNames: 'oid'
  atomicNames: 'balance'.
```

- Each clause declares instance variables with a modifier.
  `instanceVariableNames:` declares **plain** variables, as today.
- A name appears in one clause only. A repeat signals the existing
  `is declared twice in Account`.
- **Order.** `instVarNames` and `allInstVarNames` answer the names in source
  order: clause by clause as written, and names in order within each clause.
  `instVarAt:` indexes that order, as today.
- **Message form.** `subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames:`,
  with each argument a String (`''` for none). This one selector carries all
  four clauses; mixins and modifiers together in the message form go through
  `addBehavior:` after the declaration (maintainer, 2026-11-23, Q8).
- **Modifiers are inherited.**
  - A subclass cannot redeclare an inherited variable, as today.
  - Two parents (`uses:`) that declare one name with different modifiers signal
    M10.
  - Re-running a class declaration (in the REPL) that changes the modifier of
    an existing variable signals M11.
- **Reflection.** `Account instVarModifierOf: #balance` answers `#atomic`.
  `Behavior >> instVarModifierOf:` answers `#plain`, `#constant`, `#writeOnce`
  or `#atomic`, or `#field` for a variable of a value class, and signals `Error`
  for an undeclared name. `aClass isValueClass` answers whether the class is a
  value class.

| Modifier | `:=` in methods | Allowed writes | Read |
|---|---|---|---|
| plain | anywhere | any | plain |
| `constant` | only in `initialize`, outside closures | at most once, and set (not nil) by the end of construction | **checked**: reading it while unset signals M14 (§6.2) |
| `writeOnce` | anywhere | at most once, by CAS from nil; a later or losing write is silent when its value is `=` to the installed one, and signals M12 otherwise | plain |
| `atomic` | **never** (compile error) | `update:with:`, `compareAndSet:from:to:` | plain |

Every read is a read of the current snapshot. A protoCore attribute read is
one pointer, so it is never torn. A constant's read adds one nil test (§6.2);
no other read changes.

### 6.2 `constant`: assigned only during construction

- **Where it may be assigned.** `:=` to a constant variable compiles only in the
  body of an instance-side `initialize`, and in the blocks the compiler inlines
  there (`ifTrue:`, `whileTrue:`, `and:` …). Anywhere else, a real closure
  included, it signals M1 (M2 for the closure).
- **Generated constructor.** A class with constant variables (own or inherited)
  gets a class-side keyword constructor over them, inherited first:
  `Account number:owner:`. It does three things:
  1. `basicNew`;
  2. writes the given constants in one write group, on the fresh, still
     unpublished object, never writing `nil`;
  3. sends `initialize`, and answers the instance.

  The constructor cannot be redefined (M9). `new` stays available; then
  `initialize` must set the constants.
- **At run time** a constant is set at most once.
  - The store (`STORE_CONST_INSTVAR`) is a CAS from nil. Losing it signals
    `ModificationForbidden` (M3): a constant that is already set is never
    replaced.
  - Storing `nil` succeeds only when the constant is unset, and then changes
    nothing.
  - So a second explicit `anAccount initialize` that assigns a constant fails,
    and so does a racing write.
- **Set by the end of construction** (maintainer, 2026-11-23, Q4). When
  `initialize` returns inside `new` or inside the generated constructor, the
  runtime checks every constant of the class, own and inherited. One that is
  unset signals M13, and the constructor answers nothing. **`nil` counts as
  unset**, so a constant cannot be "set to nil": a field that is optional
  is write-once or plain, not constant.
  - The check is part of `Behavior >> new` and of the generated constructor.
    It runs only for a class that has constants: the class records whether it
    has any, so `new` on any other class pays one test of a class attribute
    and nothing more.
  - A class-side `new` override that calls `super new` gets the check (it runs
    in `super new`, before the override goes on). An override written
    `^ self basicNew initialize` does not: see the next point.
  - **`basicNew` stays allowed and skips this check** (maintainer,
    2026-11-23, N1 = C). It is the raw allocator, which skips `initialize`
    today, and serializers and loaders use it, then fill constants by
    reflection (§11) before anyone reads them. What `basicNew` gives up at
    construction, the read check below recovers at first use.
- **Reading an unset constant signals** (maintainer, 2026-11-23, N1 = C). Any
  read of a constant that is still unset (nil) signals **M14** instead of
  answering nil. This covers every object that left construction without the
  end-of-construction check: one made by `basicNew` and filled partly or not
  at all, and one made by a class-side `new` written in the Pharo idiom
  `^ self basicNew initialize`, whose `initialize` forgot a constant. The
  mistake surfaces at the first read, naming the constant, never as a nil that
  travels on. The paths:

  | Read path | Checks | Why |
  |---|---|---|
  | a compiled read in any method or block (`^ number`, `number + 1`), the class's own accessors included | yes: `PUSH_CONST_INSTVAR` | the compiler knows the variable is a constant, so the check sits on a new opcode and plain reads keep their opcode unchanged (§3) |
  | a read in a method installed late (LANGUAGE.md §4.10) | yes | install-time resolution looks the modifier up and emits the same checked read |
  | a read inside `initialize` before the constant is assigned (`id isNil ifTrue: [...]`) | yes | `initialize` is code like any other. A constant given to the generated constructor is set before `initialize` runs, so `initialize` reads it freely; one `initialize` must compute is assigned before it is read. "Is it set yet?" is asked with `instVarIsSet:`, not by reading |
  | `instVarAt:`, `instVarNamed:` | yes | reflection reads what a program reads; a reflective reader of an unset constant would otherwise reintroduce the nil |
  | generated readers | not applicable | readers are generated for value-class fields only (§5.2), which are not constants and are always complete; a mutable class writes its own accessors, which are compiled reads and check |
  | `instVarIsSet: #name` (new, any variable) | no; it is the probe | answers whether the variable holds a non-nil value, without signalling. Loaders and tools use it instead of catching M14 (***Proposed***) |
  | the private copy of `deepCopy` and `shallowCopy` (§11) | no | they move the attribute, unset included, without reading it as a value; the copy is checked when it is read |
  | the debugger (DAP) | no | it shows `<unset constant>`; inspecting is not a program read |
  | `compareAndSet:from:to:`, `update:with:` | not applicable | they signal M5 on any constant (§6.5) |

  - **Which nil.** A constant counts as unset when the attribute is absent or
    holds nil, as for the end-of-construction check: no constant ever holds
    nil legitimately (§6.2).
  - **Threads.** A thread that receives an object which escaped from
    `initialize` before its constants were set now gets M14 at its first read
    of one, not a silent nil.
  - **Cost.** One nil test per constant read, on the constant's own opcode;
    plain, write-once and atomic reads are unchanged. §14.1 requires it to be
    measured and sets the acceptance criterion.
- **What "constant" guarantees:**
  - an object answered by `new` or by the generated constructor has every
    constant set;
  - no read of a constant ever answers nil, however the object was made;
  - after construction, the value never changes;
  - constants given to the generated constructor are visible from the
    instance's first moment, because they are written before `initialize`
    runs.

  `initialize` of a mutable class can still let `self` escape, as today, and
  a thread that receives it there may see a constant unset. The documentation
  advises against it.

Construction is defined by *where* a constant may be assigned (compile time),
*at most once*, *set at the end of `new` or the generated constructor*, and
*never read unset* (run time). It is not tracked as a "constructing" state per object or per
frame, which would cost a write or a frame flag on every construction.

### 6.3 `writeOnce`: a CAS from nil

```smalltalk
PBKeyNode >> orderKey
  ^ orderKey ifNil: [orderKey := PBKeys orderKey: self key].
      "racers compute equal keys: each answers the one installed"
```

- `orderKey := expr` evaluates `expr`, then installs it **only if `orderKey` is
  nil**, by one compare-and-swap (`setAttributeIfEqual`, unset matching nil).
  This is `STORE_ONCE_INSTVAR`.
- **A write that finds the variable set** (it lost a race, or came later) is
  judged by equality (maintainer, 2026-11-23, Q5):
  - if the installed value `w` and the written value `v` are identical
    (`w == v`), or `w = v` answers `true`, the write is **silent**;
  - otherwise it signals **M12** (`ModificationForbidden`), and the variable
    keeps `w`.
- **The `=` used** is the installed value's: `w = v` is sent to the winner,
  late-bound, so it is the generated `=` of a value class, `ByteArray`'s
  content `=`, `String`'s, or a class's own. For an object that does not
  define `=` it is `Object`'s identity. The comparison runs on the writing
  thread, after the failed CAS, with no lock held. If `=` itself signals,
  that exception propagates, and the variable keeps `w`. An `=` that answers
  something other than a Boolean signals `NonBooleanReceiver`, as any
  condition does.
- **The value of the assignment expression is the value the variable holds
  afterwards:** the argument when this write won, or the equal winner's value
  when it lost silently. So `^ keySpec := aSpec` is protoBaseST's
  `_publishKeySpec:`.
- **Racers that legitimately compute different candidates** (a random draw,
  a place chosen by each save) say so explicitly:

  ```smalltalk
  PBAtom >> oid
    oid isNil ifTrue: [self compareAndSet: #oid from: nil to: self _newOid].
    ^ oid.          "the first candidate wins; every racer answers it"
  ```

  `compareAndSet: #f from: nil to: v` on a write-once variable never compares
  values: it answers whether this write won (§6.5). That is the form for
  "first wins, the others adopt it". `:=` is the form for "everyone publishes
  the same fact", and an unequal value there is a bug the runtime reports.
- `nil` never replaces a value. Assigning `nil` answers the current value and
  never signals.
- A write-once store is never part of a run of assignments (§6.6).

### 6.4 `atomic`: `update:with:` and `compareAndSet:from:to:`

```smalltalk
Account >> initialize
  self compareAndSet: #balance from: nil to: 0.

Account >> deposit: amount
  ^ self update: #balance with: [:old | old + amount].

Account >> withdraw: amount
  ^ self update: #balance with: [:old |
      old < amount ifTrue: [^ InvalidArgument signal: 'insufficient funds'].   "abandons the update"
      old - amount].

Account >> resetFrom: expected
  ^ self compareAndSet: #balance from: expected to: 0.
```

- **`:=` to an atomic variable is a compile error** (M4), in `initialize` too.
  Install-time and run-time paths refuse it as well (§11). An atomic variable
  starts unset (nil); `initialize` gives it a first value with
  `self compareAndSet: #balance from: nil to: 0`, as above.
- **`anObject update: #name with: aBlock`** does the following:
  1. reads the variable's current value;
  2. evaluates `aBlock value: current`;
  3. installs the result by compare-and-swap against the value read (pointer
     identity);
  4. on failure, starts again from step 1.

  It answers the value installed. The block may run several times, so it must
  be **pure**: no side effects, and no waits (M7, §10.3). A non-local return or
  an exception out of the block abandons the update, and nothing is written.
  This is `Atom >> swap:` applied to an instance variable.
- **`anObject compareAndSet: #name from: expected to: new`** installs `new` if
  the variable holds `expected` (identity; nil matches unset) and answers
  whether it did. It replaces `setInstVar:from:to:`, which is deprecated
  (§13.1), and adds the modifier and name checks of §11.
- Both messages are understood by every object. The name must be a declared
  instance variable of the receiver, or they signal M8 (`setInstVar:from:to:`
  today silently creates an undeclared attribute; its deprecated alias gains
  the same check, §13.1). The idiom is to
  wrap them in domain methods (`deposit:`), as above.
- **ABA** does not arise. The comparison is between pointers to immutable
  snapshots, so an unchanged pointer means an unchanged value (LANGUAGE.md
  §10.8).
- **No backoff, no bound.** The loop is lock-free: a failed CAS means another
  writer succeeded. This is the same as `swap:`.

### 6.5 Which messages may write which variable

| Write path | plain | constant | writeOnce | atomic | value field |
|---|---|---|---|---|---|
| `:=` (compiled) | yes | `initialize` only, at most once | CAS from nil; a finding-it-set write is silent if `=`, else M12 | compile error | `initialize` (pre-birth) only |
| `update:with:` | yes | M5 | M5 | yes | V13 |
| `compareAndSet:from:to:` (and the deprecated `setInstVar:from:to:`) | yes | M5 | only `from: nil`, answering whether it won (no equality check); otherwise M5 | yes | V13 |
| `instVarNamed:put:` / `instVarAt:put:` | yes | only while unset, else M3 | only while unset, else M6 | M6 | V13 |

Plain variables gain `update:with:` and `compareAndSet:from:to:` because
`setInstVar:from:to:` already allows a CAS on them.

All the CAS messages (`compareAndSet:from:to:`, `update:with:`) answer `true`
or `false`, or the installed value, by design: their loser adopts or retries.
Only `:=` on a write-once variable compares values, because only `:=` states
that every writer publishes the same fact. A class that wants every
write of a variable to be atomic declares it atomic.

### 6.6 Runs of assignments (LANGUAGE.md §4.5)

- A run groups only **plain** variable stores. A constant or write-once store
  (a CAS) ends the run before it, like a message send. The rule is "a run
  publishes one version, and a CAS is its own publication".
- Atomic variables are never assigned.
- The generated constructors write in one group: the value's birth (§5.3) and
  the constants of a mutable class (§6.2).
- A run is about **visibility**, not invariants. Readers see all of a run or
  none of it, but two threads running `x := x + 1. y := y + 1` on one object
  can still lose an update. §8 is the tool for invariants.

### 6.7 `Atom`

`Atom` keeps its API. An `Atom` *is* one atomic variable in an object of its
own:
- `Atom on: v` is an object whose cell holds `v`;
- `swap:` is `update: #value with:`;
- `value:ifCurrent:` is `compareAndSet:from:to:` with its arguments swapped.

The documentation says so. An object that needs a shared cell now declares an
atomic variable instead of holding an `Atom`, and saves the `Atom` object.
protoBaseST measured four `Atom` cells per atom for its write-once fields.
Reimplementing `Atom` over an atomic variable is left to the builder, if it
costs nothing (§16).

## 7. Errors in user code, by kind of state

What a programmer sees when they get it wrong is summarised here; the exact
texts are in §12.
- Assigning a value's field outside `initialize`: a compile error naming
  `withX:`.
- Assigning an atomic variable: a compile error naming `update:with:`.
- A constant outside `initialize`: a compile error. A second set at run time:
  `ModificationForbidden`. A constant still unset when `new` or the generated
  constructor ends: an `Error` naming it. A constant read while unset, on an
  object made any other way: an `Error` naming it.
- A write-once variable assigned a value not `=` to the one installed:
  `ModificationForbidden`. An equal value is silent and answers the installed
  one.
- Any refused write at run time: `ModificationForbidden`, never a silent
  no-op, and never `false` from a CAS that can never succeed.

## 8. Invariants over several fields (maintainer, 2026-11-19)

### 8.1 The pattern: one value in one atomic variable

Fields that must change together form one value class. The mutable object holds
it in one atomic variable, and every change is `update:with:` over `withX:` or
the constructor:

```smalltalk
Object valueSubclass: #Gauge
  instanceVariableNames: 'low high last'.

Gauge >> initialize
  (low <= last and: [last <= high]) ifFalse: [
    ^ InvalidArgument signal: 'Gauge: low <= last <= high'].

Object subclass: #Sensor
  atomicNames: 'gauge'.

Sensor >> initialize
  self compareAndSet: #gauge from: nil to: (Gauge low: 0 high: 0 last: 0).

Sensor >> record: x
  ^ self update: #gauge with: [:g |
      Gauge low: (g low min: x) high: (g high max: x) last: x].

Sensor >> snapshot
  ^ gauge.            "one consistent version; never a mix of two"
```

- Readers see one whole version, always valid: it passed `initialize`.
- Writers never lose an update: a CAS failure re-runs the pure block on the
  newer version.
- A reader can keep the version it read as long as it likes. It is a snapshot,
  and it costs nothing to keep.
- No STM, no lock, and no protoCore change.

This is also the Phase 2 roadmap's **state-snapshot access** (Track 16): an
actor whose state is one value in one atomic variable can be checkpointed
without quiescing it.

### 8.2 protoCore's write groups: checked, not usable for this

The question was whether protoCore's write groups could publish several
attributes conditionally. I read the protoCore 3.6.1 public header
(`headers/protoCore.h`):
- **`setAttributes`** (write groups, since 2.11.0) publishes several attributes
  as one version, atomically for readers. It is **unconditional**: when another
  writer publishes first, the whole group "is reapplied onto the newer snapshot
  and published again: every entry is 'set to value'". It cannot express "only
  if these attributes still hold what I read".
- **`setAttributeIfEqual`** is conditional, but covers **one own attribute**.
- Nothing in the public API combines the two.

So a conditional multi-attribute publish would need a protoCore change. This
spec does **not** propose one. The value-in-one-atomic-variable pattern gives
the same guarantee with what exists. The maintainer recorded the publish as a
possible future protoCore item, with no commitment (2026-11-23, Q6; §20).

## 9. One pattern from the attribute to the commit

| Level | Unit | Read | Compute | Publish if current | On conflict |
|---|---|---|---|---|---|
| attribute, raw | one variable | read it | any | `compareAndSet:from:to:` | answers `false`; the caller loops |
| attribute, managed | one variable | `update:with:` reads it | pure block | CAS | re-reads, re-runs the block |
| cell | an `Atom` | `value` | pure block | `swap:` / `value:ifCurrent:` | same |
| invariant | a value in an atomic variable | `update:with:` | block over `withX:` / `withAll:` / constructor; `initialize` validates | CAS | same |
| database | a protoBaseST transaction | snapshot at the transaction's start | the block passed to `run:` | commit | `PBConflictError`; `run:` re-runs the block with a fresh transaction, with backoff |

The same four words appear at every level: snapshot, pure computation,
conditional publish, re-run. The documentation (LANGUAGE.md and the tutorial)
teaches it once, as one pattern, with this table.

## 10. Concurrency, helping waits and the GC

### 10.1 Actors

- Values cross actor boundaries freely. They are the "immutable values" of
  LANGUAGE.md §10.8 and the Phase 2 positioning, now declarable by the user.
- **Atomic variables are safe outside the mailbox.** An object with atomic
  variables can be shared between actors and threads *without* a proxy, as an
  `Atom` can. LANGUAGE.md §10.6 consequence 2 (a direct reference bypasses the mailbox)
  still holds for plain variables. Atomic, constant and write-once variables
  stay consistent whatever path reaches them.
- `self` stays the object. `self update: #x with: …` inside an actor's method
  is a direct call, with no lock and no mailbox.
- The single-method invariant of an actor is unchanged. Modifiers add
  guarantees between threads; they remove none.

### 10.2 Value births and waits

A pre-birth `initialize` that waits behaves as §5.3 says: the worker blocks and
helps (LANGUAGE.md §10.4), and nothing is visible until birth.

### 10.3 Waits inside an `update:with:` block are an error

***Proposed:*** `wait` and `waitWithin:` evaluated while an `update:with:` block
is running on that thread signal M7. The reasons:
- The block must be pure, and a wait is not.
- A wait inside a block blocks its worker, which then **helps**: it runs other
  actors' messages on top of the wait (LANGUAGE.md §10.4). Those messages may update the
  same variable. Then the CAS fails, the block runs again and waits again: a
  livelock, not a deadlock, and the deadlock detector cannot see it.

The check costs one thread-local depth counter: `update:with:` increments it
around the block, and `wait` tests it. `compareAndSet:from:to:` runs no block,
so it needs no check.

### 10.4 Write-once and constant stores never wait

Write-once and constant stores are single CAS operations. They never wait,
allocate nothing beyond the attribute version, and never help.

### 10.5 GC

- **A value is an immutable object cell,** not an entry of the global mutable
  table:
  - it costs no shard CAS to create;
  - it is freed in the cycle that finds it dead;
  - it has none of the mutable-handle costs ("states are not roots",
    protoCore 3.1.0).

  protoBaseST measured 5+k cells per mutable instance against 2+k for an
  immutable one.
- **Pending fields** are frame temporaries until birth, rooted like any
  temporary. Birth is one `setAttributes` on the class's immutable template.
- **`update:with:`** keeps the value read and the block's result on its operand
  stack or under a `TransientPin` across the CAS, as `setInstVar:from:to:` and
  `swap:` do today.
- No new root sets, no change to the collector loop, and no `ProtoTuple` (§3).

## 11. Reflection

| Message | Plain | Constant | Write-once | Atomic | Value field |
|---|---|---|---|---|---|
| `instVarNamed:`, `instVarAt:` | reads | reads; unset signals M14 | reads | reads | reads |
| `instVarIsSet:` | whether non-nil | whether set, never signals | whether set | whether non-nil | whether non-nil |
| `instVarNamed:put:`, `instVarAt:put:` | writes | writes only while unset (M3 after) | writes only while unset (M6 after) | M6 | V13 |
| `compareAndSet:from:to:` | CAS | M5 | CAS only `from: nil` (M5 otherwise) | CAS | V13 |
| `setInstVar:from:to:` (deprecated, 0.17.0 only) | as `compareAndSet:from:to:`, after warning W1 once per process | | | | |
| `update:with:` | CAS loop | M5 | M5 | CAS loop | V13 |
| `instVarModifierOf:` | `#plain` | `#constant` | `#writeOnce` | `#atomic` | `#field` |
| `shallowCopy` / `copy` | copies | copies | copies | copies | answers the receiver |
| `deepCopy` | as today | as today (see below) | as today | as today | built through the constructor (§5.8) |

- **Unset constants and write-once variables accept reflection.** This lets
  serializers, `deepCopy` and protoBaseST loaders fill them. A reflective write
  never *replaces* a value: reflection is not a publication protocol, so a
  reflective writer that finds the variable set gets an error, even with an
  equal value (unlike `:=`, §6.3).
- **A constant filled by reflection after `basicNew`** is the caller's
  responsibility: no end-of-construction check runs (§6.2). A constant the
  loader forgot signals M14 at its first read, by any path. A loader asks
  `instVarIsSet:` when it needs to know.
- **`Object >> deepCopy`** today makes a `shallowCopy` and then writes every
  variable with `instVarNamed:put:`. For a constant or write-once variable that
  is already set, that write would fail. `deepCopy` therefore builds the copy
  with its variables in one write group (a private primitive) before the copy
  is visible. For atomic variables it writes the current value read once.
- **The debugger (DAP)** shows modified variables with their modifier. Its
  "set variable" request follows the `instVarNamed:put:` column.

## 12. Errors and their texts

Compile-time errors use the compiler's existing form,
`<text> (line N)`. For methods installed late (LANGUAGE.md §4.10), the same text is
reported when the method is installed. Run-time errors use the classes given.

**Value classes**

| Id | When | Class | Text |
|---|---|---|---|
| V1 | the superclass has instance variables and is not a value class | `Error` | `the value class Point3D needs a value class or a class without instance variables as its superclass; Base declares 'a'` |
| V2 | a `uses:` / `addBehavior:` mixin of a value class has instance variables | `Error` | `the value class Point cannot use Tagged: it declares instance variables ('tag')` |
| V3 | `subclass:` on a value class | `Error` | `Point is a value class: subclass it with valueSubclass:` |
| V4 | `constantNames:` / `writeOnceNames:` / `atomicNames:` on a value class | `Error` | `the value class Point cannot declare atomicNames:: every field of a value is fixed at birth` |
| V5 | `new` (with fields), `basicNew` or `newChild` on a value class | `Error` | `Point is a value class: make one with x:y:` |
| V6 | an inherited generated constructor sent to a value subclass | `Error` | `Point3D is made with x:y:z:, not x:y:` |
| V7 | two fields generate one selector, or a field hides an `Object` selector | `Error` | `the value class Point cannot name a field 'hash': its reader would hide Object>>hash` |
| V8 | defining the constructor or a `withX:` on a value class | compile/install error | `x:y: is the generated constructor of the value class Point and cannot be redefined; give your constructor another name` |
| V9 | `initialize` for a value class the compiler could not identify | install error | `initialize of the value class Kid must be compiled with its class declaration in view` |
| V10 | `self` or `super` in a value class's `initialize` | compile error | `self is not available in initialize of the value class Point: the value does not exist yet` |
| V11 | `initialize` sent to a value | `Error` | `initialize of the value class Point runs only before birth, from x:y:` |
| V12 | assigning a field outside `initialize` | compile/install error | `cannot assign to the field 'x' of the value class Point: a value is complete at birth; answer a new one with withX:` |
| V13 | any run-time write to a value (store, CAS, reflection) | `ModificationForbidden` | `Point is a value class: its field 'x' is fixed at birth; nothing was written` |
| V14 | a `withAll:` key that names no field | `InvalidArgument` | `withAll: Bounds has no field 'width'` |
| V15 | a `withAll:` key that names a derived field | `InvalidArgument` | `withAll: 'area' is a derived field of Rect; initialize computes it` |
| V16 | a `withAll:` field named twice | `InvalidArgument` | `withAll: names the field 'low' of Bounds twice` |
| V17 | a `withAll:` element that is not an Association, or an argument that is not a collection | `InvalidArgument` | `withAll: expects field -> value Associations, not 3` |

**Modifiers**

| Id | When | Class | Text |
|---|---|---|---|
| M1 | `:=` to a constant outside `initialize` | compile/install error | `cannot assign to the constant 'number' of Account outside initialize` |
| M2 | `:=` to a constant inside a closure in `initialize` | compile/install error | `cannot assign to the constant 'number' of Account inside a block: the block may run after construction` |
| M3 | a constant that is already set is written | `ModificationForbidden` | `the constant 'number' of an Account is already set; nothing was written` |
| M4 | `:=` to an atomic variable | compile/install error | `cannot assign to the atomic variable 'balance' of Account: use update:with: or compareAndSet:from:to:` |
| M5 | a CAS message on a constant, or one that does not start from nil on a write-once variable | `ModificationForbidden` | `update:with: cannot write the write-once variable 'oid' of a PBAtom; assign it with :=` (the selector and the modifier vary) |
| M6 | reflection replaces a write-once value, or writes an atomic variable | `ModificationForbidden` | `instVarNamed:put: cannot write the atomic variable 'balance' of an Account: use update:with: or compareAndSet:from:to:` |
| M7 | `wait` / `waitWithin:` inside an `update:with:` block | `Error` | `wait inside an update:with: block: the block must be pure and may run several times` |
| M8 | a CAS message names an undeclared variable | `Error` | `an Account has no instance variable 'zork'` |
| M9 | defining a generated constructor of constants | compile/install error | `number:owner: is the generated constructor of Account and cannot be redefined; give your constructor another name` |
| M10 | two parents declare one name with different modifiers | `Error` | `instance variable 'id' is constant in Keyed and atomic in Shared` |
| M11 | a redeclaration changes a variable's modifier | `Error` | `Account is already declared with 'balance' atomic; a class keeps the modifiers it was declared with` |
| M12 | `:=` on a write-once variable that holds a value not `=` to the one written | `ModificationForbidden` | `the write-once variable 'oid' of a PBAtom already holds a different value; nothing was written` |
| M13 | a constant unset (or nil) when `new` or the generated constructor ends | `Error` | `the constant 'owner' of an Account is not set at the end of construction (number:owner:)` (the selector is `new` or the constructor) |
| M14 | a constant read while unset (nil): compiled read, `instVarAt:` or `instVarNamed:` | `Error` | `constant 'owner' of an Account read before it was set` |

**Deprecation**

| Id | When | Where | Text |
|---|---|---|---|
| W1 | the first `setInstVar:from:to:` of the process, protoST 0.17.0 | one line on stderr, once per process | `protoST: warning: setInstVar:from:to: is deprecated and is removed in protoST 0.18.0; use compareAndSet:from:to:` |

The wording of V and M texts is fixed by this spec only where conformance
tests pin it. The pinned parts are the quoted names and the phrases *value
class*, *fixed at birth*, *already set*, *atomic variable*, *must be pure*,
*generated constructor*, *withAll:*, *already holds a different value* and
*not set at the end of construction*. M14 and W1 are pinned whole, as the 0.9.0
deprecation line was. The rest may be polished.

## 13. Migration

### 13.1 Existing protoST code

- **Nothing changes for a class that uses none of the new forms.**
  `subclass:`, `instanceVariableNames:`, plain stores, write groups and
  `Atom` behave as today.
- **Behaviour changes, each recorded in CHANGELOG and STATUS:**
  - `STORE_INSTVAR` on an immutable receiver signals `ModificationForbidden`
    instead of a `NativeError` (same condition, catchable by class).
  - **`setInstVar:from:to:` is deprecated** (maintainer, 2026-11-23, Q9):
    - in **0.17.0** it is an alias of `compareAndSet:from:to:`, with its name
      check (M8) and modifier checks (§6.5). Today it silently creates an
      attribute for an undeclared name; a program that relied on that was
      already outside LANGUAGE.md ("matched against the receiver's own
      instance variables"). The first call in a process prints warning W1
      (§12) on stderr, once, as the 0.9.0 byte-Array deprecation did;
    - in **0.18.0** it is removed: a send is `MessageNotUnderstood`.
    - Impact: protoST's own tests and examples that use it move to
      `compareAndSet:from:to:` in 0.17.0. protoBaseST 0.4.x runs on 0.17.0
      with the warning, and does not run on 0.18.0; protoBaseST 0.5.0
      migrates (§13.2) and raises its floor.
- **New deviations** (next free ids, D60 onwards):
  - value classes: their syntax, birth, pre-birth `initialize` with automatic
    chaining, no freeze, shallow immutability and the generated protocol
    (`withAll:` included);
  - the modifier clauses and their semantics: constants checked at the end of
    construction; a write-once assignment silent for an equal value and
    refused for an unequal one;
  - waits refused inside `update:with:`;
  - `setInstVar:from:to:` deprecated, then removed.
- **Kernel values that hand-roll immutability** (`UUID`, the calendar
  classes, `Decimal`: "its one private initialiser … refuses a second call")
  *may* become value classes. Their public constructors keep their names,
  because a class adds constructors that call the generated one. This is not
  required by this spec. Each one is a separate, measured change.

### 13.2 protoBaseST

| Today (protoBaseST 0.4.0) | After |
|---|---|
| `Object subclass: #PBLabeledRoot instanceVariableNames: 'label record'`, plus a class-side `label:record:` that validates, plus `basicNew _setLabel:record:` that refuses a second call | `Object valueSubclass: #PBLabeledRoot instanceVariableNames: 'label record'`; the validation moves to `initialize`; `label:record:` is the generated constructor, so the hand-written one and `_setLabel:record:` are deleted |
| The other `_set…:` initialisers of immutable values in `_ivar_allowlist.txt` (`PBOrderKey`, `PBFrozenDict`, `PBRootRecord`, `PBLogEntry`, `PBClusterMessage`, …) | Each becomes a value class. A class whose public constructor has another name keeps it, calling the generated one |
| The write-once fields `oid saved memo slots` of `PBAtom`, `orderKey` of `PBKeyNode`, `keySpecObject` of `PBIndex` (each filled by `setInstVar: #f from: nil to: x. ^ f`) | `writeOnceNames:` on each class. Each publisher then takes the form the next rows give, decided by the Q5 check below |
| `_publishOrderKey:` (racers compute `PBKeys orderKey: self key`; `PBOrderKey` defines `=`), `_publishKeySpec:` (racers parse the same data; `PBKeySpec` defines `=`), `PBAtom>>_slots` (derived from the installed memo, so identical) | `:=`: racers publish equal values, so their losses stay silent (`^ orderKey := anOrderKey`) |
| `_publishOid:` through `PBAtom >> oid` (an unsaved atom draws a random version-4 oid, `_newOid`, on first need: racers draw different oids); `_publishSaved:` in `_push:into:` (each concurrent save makes its own `_PBPlace`, which has no `=`; the loser tests `== place` and answers the winner's pointer); `_publishMemo:` / `_publishEnvelope:` for a record loaded as an envelope (each racing load builds its own `PBAtomEnvelope`, which has no `=`) | **These rely on silent unequal losses.** Under `:=` they would signal M12. They take the "first wins, the others adopt" form of §6.3: `self compareAndSet: #oid from: nil to: candidate. ^ oid`. The behaviour is unchanged |
| `PBAtom>>_publishOid:slots:memo:` (a load of a shaped record: oid, slots, then memo, so that a reader that sees the memo sees the rest; racing loads build their own `slots` Arrays) | Three `compareAndSet: … from: nil to: …` in that order, then the installed values are read. The order is kept because each CAS is its own publication (§6.6). `:=` would be correct for the oid (equal `ByteArray`s) but would compare the slots Arrays element by element, so the CAS form is kept for all three |
| `tests/unit/atom_weight.st`: its `race` and `twice` cases publish unequal second values (places at 64 and 128, order keys 1 and 2, key specs `'a'` and `'b'`, two distinct envelopes) and expect the first to be answered | The cases on fields that keep the CAS form (oid, saved, memo) pass unchanged. The order-key and key-spec cases with unequal values now expect M12, and gain an equal-value case that expects silence |
| `tests/unit/_queries.st` pins oids with `(each _publishOid: oid) = oid ifFalse: [Error signal: …]` | Unchanged: `_publishOid:` keeps the CAS form and answers the installed oid |
| Mutable holders of an open file and `Atom`s (`PBAcceptorLog`: "an open file, its lock and two Atoms") | `constantNames:` for what the constructor gives; `atomicNames:` instead of the two `Atom` objects |
| `tests/unit/values_immutable.st` with its grep and allowlist | Replaced by a reflective test: every class in `src/protobase` is a value class, or declares its variables with modifiers, and the remaining plain variables are listed and justified. The compiler and the runtime now enforce what the grep only checked |
| `PBDatabase >> run:` retry on `PBConflictError` | Unchanged. It is the top row of §9's table, and the documentation now presents it as such |

The migration is protoBaseST's own release (§15), done after the protoST
release that carries each feature. Its acceptance criteria:
- protoBaseST's whole suite passes on the new protoST;
- the atom-weight probe is re-run, and cells per index node are reported
  before and after. The figures are reported, not gated: each benchmark
  self-reports the work done.

## 14. Tests

### 14.1 Strategy

- **Conformance.** A new folder, `tests/conformance/16-explicit-state/`,
  written from this spec and LANGUAGE.md, not from the implementation. It
  uses `EXPECT:` and `EXPECT-ERROR: <pinned text>`.
- **Unit tests** (`tests/unit`): the compiler emits `STORE_CONST_INSTVAR` and
  `STORE_ONCE_INSTVAR`, never `STORE_INSTVAR`, for modified variables; a run
  ends before them; pre-birth `initialize` compiles fields as temporaries. The
  bytecode listings in `docs/VM.md` gain examples, which the documentation
  check verifies.
- **Concurrency** (`tsan` label, also run under ThreadSanitizer): the races
  listed in §14.3.
- **Documentation checks.** Every example in LANGUAGE.md and the tutorial that
  states a result is run by the existing checker.
- **Non-regression of the plain path.** Run the existing benchmarks before and
  after, measured with `perf stat -r 3` cycles, with a fresh copy of the
  library per run. Plain stores must not slow down. The figures are reported.
- **The cost of the constant read check** (N1 = C). Measured, never assumed:
  - **Benchmarks:** `benchmarks/comparable/attr_lookup.st` (100,000 × 3 plain
    instance-variable reads, existing) and a new
    `benchmarks/micro/const_read.st`, its exact twin with the three variables
    declared `constantNames: 'a b c'` and set by the generated constructor.
    Each self-reports its result (600000), and the runner verifies it.
  - **Measurement:** `perf stat -r 3` cycles of each, on 0.16.1 and on the
    0.17.0 candidate, serial, on an idle machine, with a fresh copy of the
    library per run.
  - **Acceptance:** `attr_lookup.st` on 0.17.0 is within 1 % of 0.16.1 (plain
    reads unchanged), and `const_read.st` on 0.17.0 is within 3 % of
    `attr_lookup.st` on 0.17.0 (the nil test is the whole difference). A
    result outside either bound blocks the release until it is explained and
    the maintainer accepts it. The four figures go in the release notes.
- **protoBaseST acceptance** (§13.2).

### 14.2 Conformance list

**Value classes**
1. `value-born-complete`: `(Point x: 1 y: 2) y` gives `2`.
2. `value-constructor-order`: `(Point3D x: 1 y: 2 z: 3) x` gives `1`, and the selector is `x:y:z:`.
3. `value-initialize-validates`: a bad argument signals the program's error, and no instance is observable (a class-side counter of births stays 0).
4. `value-initialize-normalises`: `(Span low: 5 high: 2) low` gives `2`.
5. `value-derived-field`: `(Rect width: 3 height: 4) area` gives `12`.
6. `value-derived-not-in-constructor`: `Rect width:height:area:` is `MessageNotUnderstood`.
7. `value-initialize-chain-root-first`: a subclass's initializer sees fields the superclass's initializer normalised, and a subclass without `initialize` still runs its superclass's.
8. `value-self-in-initialize-is-error`: V10.
9. `value-super-in-initialize-is-error`: V10.
10. `value-assign-outside-initialize-is-error`: V12.
11. `value-assign-in-block-is-error`: V12, for a block in an ordinary method.
12. `value-late-method-assign-is-install-error`: V12, reported at install, for a method on a subclass of an imported value class.
13. `value-store-refused`: `instVarNamed:put:` gives `ModificationForbidden`, and the field is unchanged.
14. `value-cas-refused-not-false`: `compareAndSet:from:to:` and `update:with:` signal; neither answers `false`.
15. `value-withX-new-value`: the receiver is unchanged, and the answer has the new field.
16. `value-withX-revalidates`: an invalid `withX:` signals.
17. `value-withX-identical-answers-receiver`: `(p withX: p x) == p`.
18. `value-withX-keeps-subclass`: `(Point3D x: 1 y: 2 z: 3) withX: 9` is a `Point3D`.
19. `value-equality-hash`: equal fields give `=` and equal `hash`; `==` is false; works as a `Dictionary` key and in a `Set`.
20. `value-new-refused`: V5 for `new`, `basicNew` and `newChild`.
21. `value-zero-fields-new`: a value class without fields answers distinct values from `new`.
22. `value-inherited-constructor-refused`: V6.
23. `value-subclass-refused`: V3.
24. `value-superclass-with-ivars-refused`: V1.
25. `value-mixin-with-ivars-refused`: V2 for `uses:` and for `addBehavior:`.
26. `value-modifier-clause-refused`: V4.
27. `value-generated-redefinition-refused`: V8 for the constructor, a `withX:` and `withAll:`.
28. `value-reader-override-allowed`: an overridden reader answers the override's value.
29. `value-field-name-hides-object-selector`: V7.
30. `value-initialize-sent-later-refused`: V11.
31. `value-copy-is-receiver`: `p copy == p`; `deepCopy` gives `=` and not `==`.
32. `value-reflection`: `Point isValueClass`, `Point instVarModifierOf: #x` gives `#field`, and `instVarNames` is `#('x' 'y')`.
33. `value-shared-between-actors`: many actors read one value, and each answers the same sum.
34. `value-message-form`: `(m Base) valueSubclass: #Kid instanceVariableNames: 'a'` with an `initialize` works.
35. `value-withAll-one-birth`: §5.4's `Bounds`: `(b withLow: 5) withHigh: 10` signals, `(b withAll: { #low -> 5. #high -> 10 }) high` gives `10`, and a class-side birth counter shows one birth, not two.
36. `value-withAll-unknown-key`: V14.
37. `value-withAll-derived-key`: V15.
38. `value-withAll-duplicate-key`: V16, also when both values are the same.
39. `value-withAll-malformed`: V17 for `{ 3 }` and for `3`.
40. `value-withAll-identical-answers-receiver`: an empty collection, and pairs with identical values, answer the receiver (`==`).
41. `value-withAll-dictionary`: a `Dictionary` argument works, and String keys work as Symbol keys.
42. `value-withAll-keeps-subclass`: on a `Point3D`, `withAll: { #x -> 9. #z -> 0 }` answers a `Point3D` with `y` copied.
43. `value-withAll-revalidates`: a `withAll:` whose final values break the invariant signals, and nothing is born.
44. `value-field-named-all-refused`: V7.
45. `value-shallow`: a value whose field holds an `OrderedCollection`: `add:` through the reader changes the collection; the field is still the same object (`==`); the value's `=` with a value built earlier with an equal collection changes, as documented.
46. `invariant-value-in-atomic`: §8.1's `Sensor`, sequentially.

**Modifiers**

47. `const-by-generated-constructor`: `(Account number: 7 owner: 'ada') number` gives `7`, and `initialize` sees it set.
48. `const-in-initialize`: `new` plus `initialize` sets a constant.
49. `const-outside-initialize-is-error`: M1.
50. `const-in-closure-is-error`: M2. An inlined `ifTrue:` is allowed.
51. `const-second-initialize-refused`: M3.
52. `const-nil-when-unset`: assigning `nil` to an unset constant changes nothing and signals nothing (then the end-of-construction check applies).
53. `const-reflection-fills-unset-only`.
54. `const-cas-refused`: M5.
55. `const-unset-after-new`: `new` on a class whose `initialize` leaves a constant unset signals M13, naming it and `new`.
56. `const-nil-from-constructor`: `Account number: 7 owner: nil` signals M13 for `owner`, naming `number:owner:`.
57. `const-inherited-unset`: a subclass whose `initialize` does not call `super initialize`, leaving an inherited constant unset, signals M13.
58. `const-new-override-checked`: a class-side `new` written `^ super new` is checked.
59. `const-basicNew-unchecked`: `basicNew` answers an object with its constants unset and signals nothing; reflection then fills them.
60. `const-check-no-constants`: a class without constants is unaffected by `new` (no error, `initialize` runs as today).
61. `once-equal-loss-silent`: with `setKey: x ^ key := x`, `o setKey: k1` then `o setKey: k2`, where `k1 := 'alpha-' , 'key'` and `k2 := 'alpha' , '-key'` (equal, not identical: longer than the 6 bytes protoCore keeps inline, and the test checks `(k1 == k2) not` first), both answer `k1`, silently.
62. `once-unequal-refused`: `o setKey: 'alpha-key'` then `o setKey: 'beta-key'` signals M12, and `key` stays `'alpha-key'`.
63. `once-equality-is-the-installed-values`: a class whose `=` answers `true` for everything makes any second write silent; a class whose `=` signals propagates that error.
64. `once-nil-never-replaces`.
65. `once-cas-adopts`: `compareAndSet: #oid from: nil to: 1`, then `… to: 2` answers `false` and signals nothing; `oid` is `1`.
66. `once-update-refused`: M5; `compareAndSet:` from anything but nil signals M5.
67. `once-reflection-rules`: M6 once set, also with an equal value.
68. `atomic-assign-is-error`: M4.
69. `atomic-update-answers-installed`.
70. `atomic-compare-and-set`: true, then false.
71. `atomic-update-nonlocal-return-abandons`: nothing is written.
72. `atomic-update-exception-abandons`: nothing is written.
73. `atomic-wait-in-update-is-error`: M7.
74. `atomic-reflection-refused`: M6.
75. `cas-undeclared-name`: M8 for `compareAndSet:from:to:` and `update:with:`.
76. `plain-update-allowed`: `update:with:` on a plain variable works.
77. `modifiers-inherited`: a subclass's `:=` to an inherited atomic variable is M4.
78. `modifier-conflict-mixins`: M10.
79. `modifier-redeclaration-refused`: M11 (REPL-style, two declarations in one file).
80. `modifier-instvarnames-order`: source order.
81. `modifier-message-form`: `subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames:`.
82. `store-immutable-receiver-is-modification-forbidden`: the class of the error raised today as a `NativeError`.
83. `deepcopy-with-modifiers`: `deepCopy` of an object with set constants and write-once variables works.
84. `runs-end-before-cas-stores`: through the documentation's bytecode listing.
85. `setinstvar-alias` (0.17.0 only): `setInstVar:from:to:` behaves as `compareAndSet:from:to:`, M8 included.
86. `setinstvar-removed` (from 0.18.0): `setInstVar:from:to:` is `MessageNotUnderstood`.
87. `const-loader-fills-by-reflection`: a loader does `basicNew`, fills both constants with `instVarNamed:put:`, and the object then reads them normally; no error at any point.
88. `const-loader-forgets-one`: the same loader fills one constant of two; reading the other (an accessor) signals M14 with the exact text; reading the filled one works.
89. `const-pharo-new-override`: a class-side `new` written `^ self basicNew initialize` whose `initialize` forgets `owner` answers the object without M13; the first `anAccount owner` signals M14.
90. `const-read-before-assignment-in-initialize`: `initialize` reads `id` before assigning it (`id isNil ifTrue: [...]`) and signals M14; reading a constant the generated constructor set works there.
91. `const-instVarAt-unset`: `instVarAt:` and `instVarNamed:` on an unset constant signal M14; on a set one they answer it.
92. `const-instVarIsSet`: `instVarIsSet:` answers false then true around a reflective fill, and never signals.
93. `const-copy-keeps-unset`: `shallowCopy` and `deepCopy` of a `basicNew` object with an unset constant succeed; the copy signals M14 when that constant is read.

**CLI** (`tests/cli`, since the conformance runner reads stdout only)

- `cli_setinstvar_deprecation` (0.17.0): a program that calls `setInstVar:from:to:` three times prints W1 on stderr exactly once.

### 14.3 Concurrency tests (`tsan` label)

- **T1:** 8 actors × 10,000 `deposit: 1` on one shared `Account` (not a
  proxy). The final balance is exactly 80,000, and the program prints the work
  it did.
- **T2:** 16 threads race on one write-once variable:
  - with values that are `=` and not identical (16 equal Strings of more than
    6 bytes, each built by concatenation, checked pairwise not `==`): every
    racer answers the installed value, and none signals;
  - with 16 different values through `:=`: exactly one write installs, the 15
    others signal M12, and the variable holds the winner's value;
  - with 16 different candidates through `compareAndSet: … from: nil to: …`:
    exactly one answers `true`, and every racer then reads the winner.
- **T3:** §8.1's `Sensor`: writers record random values while readers check
  `low <= last <= high` on every snapshot. No violation, and the counts of
  reads and writes are printed and verified.
- **T4:** constants given to the generated constructor are read correctly by
  actors that receive the object right after construction.
- **T5:** a wait inside an `update:with:` block running on a worker signals M7.
  It does not hang, including when the wait would help.
- **T6:** values built on one actor and read on another under GC pressure (a
  low ProtoSpace limit). No crash, and no premature free.

## 15. Phased delivery

| Release | Carries | Requires |
|---|---|---|
| **protoST 0.17.0**, *explicit state I: modifiers* | `constantNames:`, `writeOnceNames:`, `atomicNames:` (textual and message forms); generated constructors over constants and the end-of-construction check (M13); `STORE_CONST_INSTVAR` and `STORE_ONCE_INSTVAR` with the write-once equality rule (M12); `update:with:`, `compareAndSet:from:to:`; `setInstVar:from:to:` as a deprecated alias with its checks and warning W1; reflection (§11), `instVarModifierOf:`; `ModificationForbidden` for refused stores; M7 wait check; runs excluding CAS stores; LANGUAGE.md §4.13 (modifiers), §10.9 (the pattern, §9's table), the "locals" note (§4); the constant read check (M14, `PUSH_CONST_INSTVAR`, `instVarIsSet:`) and its measurement (§14.1, `benchmarks/micro/const_read.st`); conformance 47–85 and 87–93, `cli_setinstvar_deprecation`, T1, T2, T4, T5 | protoCore 3.6.1 (unchanged) |
| **protoST 0.18.0**, *explicit state II: value classes* | `valueSubclass:`, `derivedNames:`; pre-birth `initialize` with automatic chaining; generated constructor, readers, `withX:`, `withAll:`, `=`, `hash`; immutable instances and template; V1–V17; `isValueClass`; `deepCopy`/`copy` rules; shallow immutability documented; `setInstVar:from:to:` removed; LANGUAGE.md §4.14 (value classes) and §8.1's pattern; a tutorial section "values, versions and retries"; conformance 1–46 and 86 (85 and `cli_setinstvar_deprecation` retired), T3, T6 | protoCore 3.6.1 (unchanged) |
| **protoBaseST 0.5.0** | The migration of §13.2: values become value classes; write-once fields declared, published with `:=` or the CAS form as §13.2 decides; `Atom` fields become atomic; no `setInstVar:from:to:` left; the allowlist test becomes a reflective test. The atom-weight figures are re-measured and reported | protoST 0.18.0 |
| later, only if measured | `Atom` reimplemented over an atomic variable; kernel values (`UUID`, calendar, `Decimal`) as value classes | — |

The order is ruled (maintainer, 2026-11-23, Q10): modifiers in 0.17.0, values
in 0.18.0. Modifiers formalise patterns protoBaseST already runs, so the
migration of its write-once fields can start before value classes exist. The
removal of `setInstVar:from:to:` in 0.18.0 relies on this order: 0.17.0 is the
release that warns. Each release
updates STATUS.md, CHANGELOG.md and the deviation table. The spec is shown at
the FAST workshop (2026-12-03) as the language's direction; no release is
promised for that date.

## 16. Left to the builder

- **Storage of the marks.** How the class records modifiers and value-ness for
  the runtime (for example a private class attribute, `__ivar_modifiers__`)
  and for the install-time path (LANGUAGE.md §4.10).
- **Opcode numbering.** The numbers of `STORE_CONST_INSTVAR` and
  `STORE_ONCE_INSTVAR`, and the loader's size table. Both opcodes go through
  the same `setAttributeIfEqual` path as `setInstVar:from:to:`, including its
  absent-means-nil mapping.
- **Pre-birth compilation.** How `initialize` is compiled in pre-birth mode
  (fields as frame temporaries), and how the constructor primitive runs it.
- **The immutable template.** Its form, and how birth uses it (one
  `setAttributes`); `nil` fields are not written.
- **The generated methods.** Primitives or generated bytecode, and how
  `withX:` and `withAll:` reuse the constructor path.
- **`PUSH_CONST_INSTVAR`.** Its number, and whether a `PUSH_OWN_CONST_INSTVAR`
  twin is worth it, decided by the §14.1 measurement.
- **The end-of-construction check.** Where `new` and the generated
  constructor test it, and how a class records that it has constants, so a
  class without any pays one attribute test.
- **The write-once equality test.** How `STORE_ONCE_INSTVAR` re-reads the
  installed value and sends `=` after a failed CAS (a send from the store
  path, rooted like any send).
- **W1.** The once-per-process flag of the deprecation warning, as the 0.9.0
  byte-Array warning did it.
- **`update:with:`.** Whether it is a primitive or a kernel method over
  `compareAndSet:from:to:`, provided M7 and the purity rules hold.
- **The wait check.** Where the thread-local depth counter for M7 lives.
- **`deepCopy`.** The private one-group copy primitive it uses.
- **Wording.** The wording of the unpinned parts of the error texts (§12).
- **`Atom`.** Whether to reimplement it over an atomic variable (§15, "later").

## 17. Decisions

### 17.1 Maintainer rulings

**2026-11-19, scope** (binding):
- Born complete, no freeze, no builder blocks. §5.3, §5.8.
- Locals unchanged. §4.
- Value classes, the three modifiers, and one value in one atomic variable for
  invariants; no STM. §5, §6, §8.
- The optimistic pattern taken to the user, and the Croquet link as
  direction. §9, §19.

**2026-11-23, rulings on the first draft's open questions, and on N1:**

| Q | Question | Ruling | Where |
|---|---|---|---|
| Q1 | Chaining of pre-birth initializers | Every value class's `initialize` runs automatically, root first; `super` is forbidden | §5.3, V10, tests 7 and 9 |
| Q2 | Updating several fields | `withAll:` is generated, taking `field -> value` pairs: one birth, one validation; unknown, derived and duplicate keys are errors; chained `withX:` is not the answer, because an intermediate value can break a cross-field invariant | §5.2, §5.4, V14–V17, tests 35–44 |
| Q3 | Shallow or deep immutability | Shallow, documented as such | §5.6, test 45 |
| Q4 | What "construction" means for constants | Also set by the end of construction: checked when `new` or the generated constructor ends; nil counts as unset | §6.2, M13, tests 55–60 |
| Q5 | A write-once write that finds a different value | Silent when the value is `=` to the installed one (the installed value's `=`); `ModificationForbidden` otherwise. protoBaseST's oid, saved and memo publishers rely on silent unequal losses and move to the `compareAndSet:` form | §6.3, §13.2, M12, tests 61–65, T2 |
| Q6 | Conditional multi-attribute publish | Recorded as a possible future protoCore item, with no commitment | §8.2, §20 |
| Q7 | Generated readers | Yes, for every field, overridable | §5.2 |
| Q8 | Message-form combinations | One selector with all four clauses; mixins through `addBehavior:` | §6.1 |
| Q9 | Names | As specified. `setInstVar:from:to:` is deprecated: an alias with warning W1 in 0.17.0, removed in 0.18.0 | §6.4, §12, §13.1, tests 85–86 |
| Q10 | Order of the releases | Modifiers in 0.17.0, values in 0.18.0 | §15 |
| Q11 | Replicated world | On the volunteer menu now, as an L research item | §19 |
| N1 | `basicNew` on a class with constants (a question that arose while applying Q4) | Option C: `basicNew` stays allowed, so loaders fill constants by reflection; every read of an unset constant signals M14, which covers `^ self basicNew initialize` at first use. The Q4 check stays on `new` and the generated constructor | §6.2, §11, M14, §14.1 (cost), tests 87–93 |

### 17.2 Decisions this spec makes (***Proposed***)

These fill gaps the scope left open. The maintainer reviews them with the
full text.
- **Late binding.** Every check runs at compile time when the compiler can see
  the class, at install time otherwise (LANGUAGE.md §4.10), and always at run
  time. No silent failure. §1, §5.5.
- **A refused CAS signals; it never answers `false`.** §5.6, §6.5.
- **CAS stores end a run of assignments.** §6.6.
- **Reflection fills unset constant and write-once variables and never
  replaces them, even with an equal value.** §11.
- **Generated constructors, `withX:` and `withAll:` cannot be redefined;
  readers, `=` and `hash` can.** §5.2.
- **Generated `=` and `hash`** over the non-derived fields. §5.2.
- **`withX:` and `withAll:` with identical values answer the receiver.** §5.4.
- **`deepCopy` of a value is built through the constructor.** §5.8.
- **Waits inside `update:with:` blocks are errors.** §10.3.
- **Subclasses of a value class are value classes.** §5.1.
- **The compiler recognises a message-form value-class declaration with a
  literal class name**, so its `initialize` compiles in pre-birth mode; any
  other value class cannot have one (V9). §5.1.
- **`compareAndSet: … from: nil to: …` on a write-once variable never compares
  values**: it is the "first wins, the others adopt" form. §6.3.
- **`instVarIsSet:`**, a non-signalling probe, so loaders and tools need not
  catch M14. §6.2, §11.
- **Copies and the debugger do not check constant reads**: they move or show
  the attribute without reading it as a value. §6.2.
- **`withAll:` errors are `InvalidArgument`; M13 is an `Error`.** §12.

## 18. Open questions

None. The first draft's eleven questions and N1, which arose while applying
Q4, are ruled (§17.1).

## 19. Direction: versions, snapshots and replicated worlds

This section is direction, not a commitment.

**Reed, 1978.** David P. Reed's MIT doctoral thesis, *Naming and
Synchronization in a Decentralized Computer System* (MIT/LCS/TR-205), models
an object as a **sequence of immutable versions**, each valid over a range of
*pseudo-time*. A computation reads the versions current at its pseudo-time and
proposes new ones. Croquet's TeaTime replication protocol descends from this
work.

**What protoCore already gives.**
- A mutable protoCore object is already a reference to its current immutable
  version.
- A value class makes the version a first-class object: something the program
  names, keeps and compares.
- An atomic variable is a version reference, and `update:with:` is a
  conditional step from one version to the next.
- **Snapshots** are references kept.
- **Histories** are lists of versions that share structure.

All of this comes almost free, and it is the same machinery protoBaseST
persists.

**What remains real work.**
- **Deterministic execution across replicas.** Floats must be bit-identical
  across platforms, and protoST already records a case where they are not:
  D58, the platform `pow`. The actor scheduler is nondeterministic (worker
  pool, ready queues, D59). Timers read the monotonic clock.
- **A reflector**, a sequencer that stamps and orders the external messages
  every replica executes.
- **Message time instead of wall time** inside the replicated world
  (`Future after:` would read the reflector's time).
- **Naming and identity** of objects across replicas.

None of this is in the releases of §15. The explicit-state work is the
precondition: a replicated world can only be built from state whose versions
are explicit.

**Volunteer menu item** (maintainer, 2026-11-23, Q11). The FAST workshop
(2026-12-03) ends with a menu of projects for volunteers. This one goes on it
now:
- **Title:** a replicated world on protoST + protoBaseST.
- **Size:** L. **Kind:** research. There is no fixed deliverable: the outcome
  is a measured prototype and a written account of what determinism costs.
- **Prerequisites:** protoST 0.18.0 (value classes and atomic variables);
  reading Reed (1978) and the Croquet/TeaTime papers; comfort with actors and
  floating point.
- **First step:** two protoST processes on one machine, each running the same
  small world (one atomic variable holding one value), fed the same message
  sequence by a minimal reflector process; compare the worlds' versions after
  every message, and list each divergence with its cause (scheduling, timers,
  floats).

## 20. Future protoCore items

This section records protoCore items the maintainer noted, with **no
commitment**: no release, no design, and nothing in this spec depends on
them. protoST does not ask for them.

- **A conditional multi-attribute publish** (maintainer, 2026-11-23, Q6). Today
  `setAttributes` publishes several attributes as one version but
  unconditionally, and `setAttributeIfEqual` is conditional for one own
  attribute (§8.2). An operation that publishes several attributes only if
  each still holds the value read would let a mutable object keep a
  multi-field invariant without grouping the fields into one value. §8.1's
  pattern covers the need today. Anyone who takes it up starts from
  protoCore's own design process, not from this spec.
