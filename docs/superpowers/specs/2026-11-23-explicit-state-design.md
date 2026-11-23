# protoST: explicit state (value classes and instance-variable modifiers)

Status: **draft for review** (2026-11-23). Scope approved by the maintainer on
2026-11-19. Author: Gustavo Marino, with Claude.
This is a design spec only: nothing here is implemented yet.

Read first: LANGUAGE.md §4.4–§4.5 (instances, instance variables, runs of
assignments), §4.10 (methods installed late), §10.4 (helping waits), §10.6
(the synchronization boundary) and §10.8 (`Atom`, `setInstVar:from:to:`).

Bare section numbers (§5.3) refer to this spec; sections of the language
reference are written "LANGUAGE.md §n".

Markers used below:
- **(maintainer, 2026-11-19)**: part of the approved scope; binding.
- ***Proposed***: a decision this spec makes to fill a gap the scope left
  open. Each one is listed again in §17 (settled here) or §18 (needs the
  maintainer).

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
- protoBaseST can delete its allowlist convention and its hand-written
  `_publish…:` and `_set…:` initialisers, and keep the same behaviour (§13).
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
- **Deep immutability** checks on field values (open question Q3).
- A conditional multi-attribute write in protoCore (§8.2; no protoCore change
  is proposed).

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
  `getOwnAttributeDirect`. Anything that would need protoCore is an open
  question (§18). The protoCore floor stays 3.6.1.
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
| instance | **readers** (`x`, `y`, `area`): one per field, derived included (***Proposed***) | Answers the field. |
| instance | **`=`** and **`hash`** (***Proposed***) | `=`: same class (`==`) and every non-derived field `=`. `hash`: combines the class and the non-derived fields' hashes, deterministic within a run. |

**Rules:**
- **The constructor and `withX:` cannot be redefined.** Defining a method with
  either selector on that class signals V8. Both are the only way a value is
  born, so they carry its invariants. A class adds more constructors under
  other names, and they call the generated one:
  `Point class >> origin ^ self x: 0 y: 0`.
- **Readers, `=` and `hash` can be overridden.** They take no part in birth.
- **Names:** `withX:` is `with` followed by the field name with its first
  letter uppercased (`x` gives `withX:`, `lowBound` gives `withLowBound:`); a
  leading underscore is kept (`_oid` gives `with_oid:`).
  - Two fields that generate the same selector signal V7.
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
   defines `initialize` (***Proposed***: automatic chaining, open question Q1).
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
  `super`, because chaining is automatic (Q1). Helpers are class-side methods
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

### 5.4 Updates: `withX:`

- `p withX: 3` answers a new value. The receiver is unchanged.
- It is exactly the receiver's class's constructor, called with the receiver's
  non-derived fields and `x` replaced. So every pre-birth initializer runs again
  and the invariants hold for the new value.
- If the new field value is identical (`==`) to the current one, `withX:`
  answers the **receiver** and nothing is born (***Proposed***). The receiver
  already passed `initialize` with these exact fields.
- Several fields that must change together are changed through the
  constructor, or through a class-side method the class writes. Chaining
  `withX:` calls passes through intermediate values, and an intermediate value
  may violate an invariant: `(Span low: 0 high: 3) withLow: 5` normalises to
  `3..5`. Open question Q2 asks whether a multi-field `withAll:` should be
  generated.

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
  - **CAS primitives.** `setInstVar:from:to:`, `compareAndSet:from:to:` and
    `update:with:` on a value signal `ModificationForbidden`. They **never
    answer `false`**, because a retry loop would spin for ever on a write that
    can never succeed.
  - **Reflection.** `instVarNamed:put:` and `instVarAt:put:` signal
    `ModificationForbidden` too (§11).
- Identity is kept. Two constructions with equal fields are two objects: `=` is
  true and `==` is false. Values are not interned.
- A value's fields may refer to mutable objects. Immutability is **shallow**:
  the binding of a field never changes, but what the field refers to may
  (open question Q3).
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
  with each argument a String (`''` for none). Mixins and modifiers together in
  the message form need `addBehavior:` after the declaration (§18 Q8).
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
| `constant` | only in `initialize`, outside closures | at most once (a set value is never replaced) | plain |
| `writeOnce` | anywhere | at most once, by CAS from nil; a later or losing write answers the installed value | plain |
| `atomic` | **never** (compile error) | `update:with:`, `compareAndSet:from:to:` | plain |

All reads are plain reads of the current snapshot. A protoCore attribute read
is one pointer, so it is never torn.

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

  The constructor cannot be redefined (M9). `new` stays available; it leaves
  the constants unset unless `initialize` sets them.
- **At run time** a constant is set at most once.
  - The store (`STORE_CONST_INSTVAR`) is a CAS from nil. Losing it signals
    `ModificationForbidden` (M3): a constant that is already set is never
    replaced.
  - Storing `nil` succeeds only when the constant is unset, and then changes
    nothing.
  - So a second explicit `anAccount initialize` that assigns a constant fails,
    and so does a racing write.
- **What "constant" guarantees:**
  - after construction, the value never changes;
  - constants given to the generated constructor are visible from the
    instance's first moment, because they are written before `initialize`
    runs.

  `initialize` of a mutable class can still let `self` escape, as today. The
  documentation advises against it.

***Proposed (Q4):*** construction is defined by *where* a constant may be
assigned (compile time) plus *at most once* (run time), not by tracking a
"constructing" state per object or per frame. Tracking that state would cost
writes or frame flags. It would also break the Pharo idiom
`^ self basicNew initialize`, which this rule supports.

### 6.3 `writeOnce`: a CAS from nil

```smalltalk
PBAtom >> oid
  ^ oid ifNil: [oid := self _newOid].      "a racing caller answers the winner's oid"
```

- `oid := expr` evaluates `expr`, then installs it **only if `oid` is nil**, by
  one compare-and-swap (`setAttributeIfEqual`, unset matching nil). This is
  `STORE_ONCE_INSTVAR`.
- **The value of the assignment expression is the value the variable holds
  afterwards:** the argument when this write won, or the earlier or racing
  winner's value when it lost. So `^ oid := anOid` is exactly protoBaseST's
  `_publishOid:`.
- A lost write is **not** an error (open question Q5): racers that compute the
  same fact publish one of them, and every racer answers it.
- `nil` never replaces a value. Assigning `nil` answers the current value.
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
  whether it did. This is `setInstVar:from:to:`, with the modifier and
  name checks of §11.
- Both messages are understood by every object. The name must be a declared
  instance variable of the receiver, or they signal M8 (`setInstVar:from:to:`
  today silently creates an undeclared attribute; see §13.1). The idiom is to
  wrap them in domain methods (`deposit:`), as above.
- **ABA** does not arise. The comparison is between pointers to immutable
  snapshots, so an unchanged pointer means an unchanged value (LANGUAGE.md
  §10.8).
- **No backoff, no bound.** The loop is lock-free: a failed CAS means another
  writer succeeded. This is the same as `swap:`.

### 6.5 Which messages may write which variable

| Write path | plain | constant | writeOnce | atomic | value field |
|---|---|---|---|---|---|
| `:=` (compiled) | yes | `initialize` only, at most once | CAS from nil | compile error | `initialize` (pre-birth) only |
| `update:with:` | yes | M5 | M5 | yes | V13 |
| `compareAndSet:from:to:` / `setInstVar:from:to:` | yes | M5 | only `from: nil`; otherwise M5 | yes | V13 |
| `instVarNamed:put:` / `instVarAt:put:` | yes | only while unset, else M3 | only while unset, else M6 | M6 | V13 |

Plain variables gain `update:with:` and `compareAndSet:from:to:` because
`setInstVar:from:to:` already allows a CAS on them. A class that wants every
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
  `ModificationForbidden`.
- A write-once variable: never an error; the assignment answers the installed
  value.
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
the same guarantee with what exists. It is listed for the maintainer's
awareness only (Q6).

## 9. One pattern from the attribute to the commit

| Level | Unit | Read | Compute | Publish if current | On conflict |
|---|---|---|---|---|---|
| attribute, raw | one variable | read it | any | `compareAndSet:from:to:` | answers `false`; the caller loops |
| attribute, managed | one variable | `update:with:` reads it | pure block | CAS | re-reads, re-runs the block |
| cell | an `Atom` | `value` | pure block | `swap:` / `value:ifCurrent:` | same |
| invariant | a value in an atomic variable | `update:with:` | block over `withX:` / constructor; `initialize` validates | CAS | same |
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
| `instVarNamed:`, `instVarAt:` | reads | reads | reads | reads | reads |
| `instVarNamed:put:`, `instVarAt:put:` | writes | writes only while unset (M3 after) | writes only while unset (M6 after) | M6 | V13 |
| `setInstVar:from:to:` | CAS | M5 | CAS only `from: nil` (M5 otherwise) | CAS | V13 |
| `compareAndSet:from:to:` | CAS | M5 | as `setInstVar:from:to:` | CAS | V13 |
| `update:with:` | CAS loop | M5 | M5 | CAS loop | V13 |
| `instVarModifierOf:` | `#plain` | `#constant` | `#writeOnce` | `#atomic` | `#field` |
| `shallowCopy` / `copy` | copies | copies | copies | copies | answers the receiver |
| `deepCopy` | as today | as today (see below) | as today | as today | built through the constructor (§5.8) |

- **Unset constants and write-once variables accept reflection.** This lets
  serializers, `deepCopy` and protoBaseST loaders fill them. A reflective write
  never *replaces* a value: reflection is not a publication protocol, so a
  reflective writer that finds the variable set gets an error, not the
  winner's value.
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

The wording of V and M texts is fixed by this spec only where conformance
tests pin it. The pinned parts are the quoted names and the phrases *value
class*, *fixed at birth*, *already set*, *atomic variable*, *must be pure*
and *generated constructor*. The rest may be polished.

## 13. Migration

### 13.1 Existing protoST code

- **Nothing changes for a class that uses none of the new forms.**
  `subclass:`, `instanceVariableNames:`, plain stores, write groups and
  `Atom` behave as today.
- **Behaviour changes, each recorded in CHANGELOG and STATUS:**
  - `STORE_INSTVAR` on an immutable receiver signals `ModificationForbidden`
    instead of a `NativeError` (same condition, catchable by class).
  - `setInstVar:from:to:` checks that the name is a declared instance
    variable (M8) and honours modifiers (§6.5). Today it silently creates an
    attribute for an undeclared name. A program that relied on that was
    already outside LANGUAGE.md ("matched against the receiver's own instance
    variables").
- **New deviations** (next free ids, D60 onwards):
  - value classes: their syntax, birth, pre-birth `initialize`, no freeze and
    the generated protocol;
  - the modifier clauses and their semantics, including the write-once
    assignment that answers the winner;
  - waits refused inside `update:with:`.
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
| `_publishOid:`, `_publishSaved:`, `_publishMemo:`, `_publishOrderKey:`, `_publishKeySpec:`, `_slots` (`setInstVar: #f from: nil to: x. ^ f`) | `writeOnceNames: 'oid saved memo slots'`; each method body becomes `^ oid := anOid`, or the callers assign directly |
| `PBAtom>>_publishOid:slots:memo:` (several fields, one at a time, memo last so that a reader that sees the memo sees the rest) | Unchanged in meaning: three write-once assignments, in that order. A write-once store is its own publication, so the order is kept (§6.6) |
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
- **protoBaseST acceptance** (§13.2).

### 14.2 Conformance list

**Value classes**
1. `value-born-complete`: `(Point x: 1 y: 2) y` gives `2`.
2. `value-constructor-order`: `(Point3D x: 1 y: 2 z: 3) x` gives `1`, and the selector is `x:y:z:`.
3. `value-initialize-validates`: a bad argument signals the program's error, and no instance is observable (a class-side counter of births stays 0).
4. `value-initialize-normalises`: `(Span low: 5 high: 2) low` gives `2`.
5. `value-derived-field`: `(Rect width: 3 height: 4) area` gives `12`.
6. `value-derived-not-in-constructor`: `Rect width:height:area:` is `MessageNotUnderstood`.
7. `value-initialize-chain-root-first`: a subclass's initializer sees fields the superclass's initializer normalised.
8. `value-self-in-initialize-is-error`: V10.
9. `value-super-in-initialize-is-error`: V10.
10. `value-assign-outside-initialize-is-error`: V12.
11. `value-assign-in-block-is-error`: V12, for a block in an ordinary method.
12. `value-late-method-assign-is-install-error`: V12, reported at install, for a method on a subclass of an imported value class.
13. `value-store-refused`: `instVarNamed:put:` gives `ModificationForbidden`, and the field is unchanged.
14. `value-cas-refused-not-false`: `setInstVar:from:to:`, `compareAndSet:from:to:` and `update:with:` signal; none answers `false`.
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
27. `value-generated-redefinition-refused`: V8.
28. `value-reader-override-allowed`: an overridden reader answers the override's value.
29. `value-field-name-hides-object-selector`: V7.
30. `value-initialize-sent-later-refused`: V11.
31. `value-copy-is-receiver`: `p copy == p`; `deepCopy` gives `=` and not `==`.
32. `value-reflection`: `Point isValueClass`, `Point instVarModifierOf: #x` gives `#field`, and `instVarNames` is `#('x' 'y')`.
33. `value-shared-between-actors`: many actors read one value, and each answers the same sum.
34. `value-message-form`: `(m Base) valueSubclass: #Kid instanceVariableNames: 'a'` with an `initialize` works.

**Modifiers**

35. `const-by-generated-constructor`: `(Account number: 7 owner: 'ada') number` gives `7`, and `initialize` sees it set.
36. `const-in-initialize`: `new` plus `initialize` sets a constant.
37. `const-outside-initialize-is-error`: M1.
38. `const-in-closure-is-error`: M2. An inlined `ifTrue:` is allowed.
39. `const-second-initialize-refused`: M3.
40. `const-nil-when-unset`: assigning `nil` to an unset constant changes nothing and signals nothing.
41. `const-reflection-fills-unset-only`.
42. `const-cas-refused`: M5.
43. `once-assignment-answers-installed`: with `setOid: x ^ oid := x`, `o setOid: 1` gives `1`, then `o setOid: 2` gives `1`.
44. `once-nil-never-replaces`.
45. `once-update-refused`: M5. `compareAndSet: from: nil` works, from anything else signals M5.
46. `once-reflection-rules`: M6 once set.
47. `atomic-assign-is-error`: M4.
48. `atomic-update-answers-installed`.
49. `atomic-compare-and-set`: true, then false.
50. `atomic-update-nonlocal-return-abandons`: nothing is written.
51. `atomic-update-exception-abandons`: nothing is written.
52. `atomic-wait-in-update-is-error`: M7.
53. `atomic-reflection-refused`: M6.
54. `cas-undeclared-name`: M8 for `compareAndSet:from:to:`, `update:with:` and `setInstVar:from:to:`.
55. `plain-update-allowed`: `update:with:` on a plain variable works.
56. `modifiers-inherited`: a subclass's `:=` to an inherited atomic variable is M4.
57. `modifier-conflict-mixins`: M10.
58. `modifier-redeclaration-refused`: M11 (REPL-style, two declarations in one file).
59. `modifier-instvarnames-order`: source order.
60. `modifier-message-form`: `subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames:`.
61. `store-immutable-receiver-is-modification-forbidden`: the class of the error raised today as a `NativeError`.
62. `deepcopy-with-modifiers`: `deepCopy` of an object with set constants and write-once variables works.
63. `runs-end-before-cas-stores`: through the documentation's bytecode listing.
64. `invariant-value-in-atomic`: §8.1's `Sensor`, sequentially.

### 14.3 Concurrency tests (`tsan` label)

- **T1:** 8 actors × 10,000 `deposit: 1` on one shared `Account` (not a
  proxy). The final balance is exactly 80,000, and the program prints the work
  it did.
- **T2:** 16 threads race `oid := <distinct value>` on one object. Every racer
  answers the same value, and that value is one of the candidates.
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
| **protoST 0.17.0**, *explicit state I: modifiers* | `constantNames:`, `writeOnceNames:`, `atomicNames:` (textual and message forms); generated constructors over constants; `STORE_CONST_INSTVAR` and `STORE_ONCE_INSTVAR`; `update:with:`, `compareAndSet:from:to:`; `setInstVar:from:to:` checks; reflection (§11), `instVarModifierOf:`; `ModificationForbidden` for refused stores; M7 wait check; runs excluding CAS stores; LANGUAGE.md §4.13 (modifiers), §10.9 (the pattern, §9's table), the "locals" note (§4); tests 35–63, T1, T2, T4, T5 | protoCore 3.6.1 (unchanged) |
| **protoST 0.18.0**, *explicit state II: value classes* | `valueSubclass:`, `derivedNames:`; pre-birth `initialize`; generated constructor, readers, `withX:`, `=`, `hash`; immutable instances and template; V1–V13; `isValueClass`; `deepCopy`/`copy` rules; LANGUAGE.md §4.14 (value classes) and §8.1's pattern; a tutorial section "values, versions and retries"; tests 1–34 and 64, T3, T6 | protoCore 3.6.1 (unchanged) |
| **protoBaseST 0.5.0** | The migration of §13.2: values become value classes, `_publish…:` becomes write-once, `Atom` fields become atomic, and the allowlist test becomes a reflective test. The atom-weight figures are re-measured and reported | protoST 0.18.0 |
| later, only if measured | `Atom` reimplemented over an atomic variable; kernel values (`UUID`, calendar, `Decimal`) as value classes | — |

The two protoST releases can ship in either order. Modifiers come first
because they formalise patterns protoBaseST already runs, so the migration of
its write-once fields can start before value classes exist. Each release
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
  `withX:` reuses the constructor path.
- **`update:with:`.** Whether it is a primitive or a kernel method over
  `compareAndSet:from:to:`, provided M7 and the purity rules hold.
- **The wait check.** Where the thread-local depth counter for M7 lives.
- **`deepCopy`.** The private one-group copy primitive it uses.
- **Wording.** The wording of the unpinned parts of the error texts (§12).
- **`Atom`.** Whether to reimplement it over an atomic variable (§15, "later").

## 17. Decisions settled in this spec

- **Born complete, no freeze, no builders** (maintainer, 2026-11-19). §5.3,
  §5.8.
- **Locals unchanged** (maintainer). §4.
- **Value in one atomic variable for invariants; no STM** (maintainer). §8.
- **Late binding.** Every check runs at compile time when the compiler can
  see the class, at install time otherwise (LANGUAGE.md §4.10), and always at run time. No silent
  failure. §1, §5.5.
- **A refused CAS signals; it never answers `false`.** §5.6, §6.5.
- **Constant means "assigned only in `initialize` or the generated
  constructor, and at most once"**, not tracked per object (Q4). §6.2.
- **A write-once assignment answers the installed value** (protoBaseST's
  `_publish…:` semantics; Q5). §6.3.
- **CAS stores end a run of assignments.** §6.6.
- **Reflection fills unset constant and write-once variables, and never
  replaces them.** §11.
- **Generated constructors and `withX:` cannot be redefined; readers, `=` and
  `hash` can.** §5.2.
- **`withX:` with an identical value answers the receiver.** §5.4.
- **Waits inside `update:with:` blocks are errors.** §10.3.
- **Subclasses of a value class are value classes.** §5.1.
- **`setInstVar:from:to:` gains the name and modifier checks.** §13.1.

## 18. Open questions for the maintainer

- **Q1. Chaining of pre-birth initializers.** This spec runs every value
  class's `initialize` automatically, root first, and forbids `super`, so a
  subclass cannot skip its superclass's invariants. The alternative is the
  Smalltalk convention (only the most derived class runs, and calls
  `super initialize`). Which one?
- **Q2. Multi-field updates.** Should a generic `withAll: { #low -> 5. #high -> 10 }`
  (one birth) be generated, or should changes to several fields go through the
  constructor and class-side methods only?
- **Q3. Shallow or deep immutability.** Should the constructor refuse field
  values that are mutable objects (accepting only values, immutable kernel
  types and actor references)? Or should immutability stay shallow, as
  specified?
- **Q4. "Construction" for constants.** Is "assigned only in `initialize` or
  the generated constructor, at most once" enough? Or is a constant also
  required to be set by the end of construction, which needs a runtime check
  after `initialize`?
- **Q5. A write-once race lost with an unequal value.** Should it stay silent
  (the expression answers the winner, protoBaseST's current semantics)? Or
  should it signal when the losing value is not `=` to the winner?
- **Q6. Conditional group writes.** protoCore has no conditional
  multi-attribute publish (§8.2), and this spec needs none. Do you want it
  recorded as a possible protoCore item, or dropped?
- **Q7. Generated readers.** Are readers for every field generated (as
  specified), or do value classes write their own accessors, as mutable
  classes do?
- **Q8. Message-form combinations.** Is one message-form selector with all
  four clauses enough, with mixins added through `addBehavior:`? Or should
  `uses:` combinations be provided too?
- **Q9. Names.** Are `valueSubclass:`, `derivedNames:`, `constantNames:`,
  `writeOnceNames:`, `atomicNames:`, `update:with:`, `compareAndSet:from:to:`
  and `instVarModifierOf:` the right names? And should `setInstVar:from:to:`
  be deprecated in favour of `compareAndSet:from:to:`?
- **Q10. Order of the releases.** Modifiers in 0.17.0 and values in 0.18.0
  (as proposed), or values first, for the cell savings in protoBaseST?
- **Q11. Croquet.** Should a "replicated world on protoST + protoBaseST" item
  go on the volunteer menu now, with §19's list of what remains (determinism
  of floats and scheduling, a reflector, message time)?

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
