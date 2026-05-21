# Chapter 11 — The advanced object model

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 10](10-actors-and-futures.md) · Next: [Chapter 12 — Tooling](12-tooling.md)

---

[Chapter 5](05-classes-and-methods.md) taught the object model as classic
single-inheritance OO, and noted in passing that protoST is *prototype-based*
underneath. This chapter cashes in that remark. The prototype kernel makes two
things possible that a rigid class system cannot: a class with **several
parents** (multiple inheritance and mixins, via `uses:`), and a class whose
behaviour is **assembled at runtime** (via `addBehavior:`). Both are protoST
extensions — not standard Smalltalk-80.

## 11.1 Why a prototype kernel matters here

Recall the kernel model: a "class" is an object acting as a *prototype*, an
"instance" is a *child* that delegates messages it does not define up to its
parent, and "inheritance" is following that parent chain. Single inheritance is
just the special case where each prototype has *one* parent.

But protoCore — the kernel — does not restrict a prototype to one parent. A
prototype can have several. protoST exposes that capability through two
language features. Neither is bolted onto a class system after the fact; both
are the multi-parent kernel showing through.

## 11.2 Multiple inheritance and mixins: `uses:`

A class can be declared with **more than one parent**. The primary superclass
is named the usual way (after `subclass:`); additional parents go in a `uses:`
clause, as a collection of class objects:

```smalltalk
Object subclass: #Money
  instanceVariableNames: 'cents'
  uses: { Comparable. Printable }.
```

`Money`'s primary superclass is `Object`; its *additional* parents are
`Comparable` and `Printable`. A `Money` instance understands every message
defined by `Object`, by `Comparable`, by `Printable`, and by `Money` itself.

A **mixin** is not a special kind of entity. It is just a class — usually one
that defines behaviour but is never instantiated on its own. "Mixing in" means
listing that class in a `uses:` clause. `Comparable` below is a mixin: a bundle
of comparison methods that any class with a `compareTo:` can absorb.

```smalltalk
"-- mixin.st --"
Object subclass: #Comparable
  instanceVariableNames: ''.

Comparable >> > other
  ^ (self compareTo: other) > 0.

Comparable >> < other
  ^ (self compareTo: other) < 0.

Object subclass: #Money
  instanceVariableNames: 'cents'
  uses: { Comparable }.

Money >> setCents: c
  cents := c.
  ^ self.

Money >> cents
  ^ cents.

Money >> compareTo: other
  ^ cents - other cents.

a := Money new setCents: 500.
b := Money new setCents: 300.
(a > b) printString , ' and ' , (a < b) printString.
```

```bash
$ ./build/protost mixin.st
a Boolean and a Boolean
```

`Money` defines only `compareTo:`. It *inherits* `>` and `<` from the
`Comparable` mixin — and those inherited methods call back into `Money`'s
`compareTo:`. `a > b` answers `true` (500 cents exceeds 300); `a < b` answers
`false`.

(A note on the output: printing a boolean shows `a Boolean` rather than
`true`/`false` on the current build. The booleans *are* correct — `a > b` is
genuinely the boolean `true`. When you need a textual `'true'`/`'false'`, drive
a conditional: `(a > b) ifTrue: [ 'true' ] ifFalse: [ 'false' ]`.)

> **In Python** this is multiple inheritance: `class Money(Comparable):` — or,
> for true mixin style, `class Money(Comparable, Printable):`. **In
> JavaScript** there is no multiple inheritance; mixins are faked by copying
> methods onto a prototype with `Object.assign`. **In protoST** `uses:` is real
> multiple inheritance through the kernel's multi-parent prototype chain — and
> a mixin is, refreshingly, just a class. There is no separate `mixin` keyword
> to learn, no `Object.assign` ritual: you write an ordinary class and list it
> in `uses:`.

### Resolution order

When a message could be found on more than one parent, protoST resolves it
**depth-first, left-to-right**: the primary superclass subtree first, then each
`uses:` mixin subtree in the order listed. The *diamond* case — a selector
reachable through two parents that share an ancestor — resolves to the **first**
in that order, and a shared ancestor is visited once. `super` from a method of a
multiply-inheriting class searches the same order, starting after the method's
defining class.

You will rarely need to think about this; the rule is simply "the order you
wrote them". But it is deterministic, which is what you want.

### Mixin instance variables — use accessors

A mixin may declare its own instance variables, and they combine with the using
class's own. There is a practical caveat on the current build, and it is worth
stating plainly:

> A mixin's instance variable is reliably reached **through an accessor
> method**, not by writing the bare variable name inside a method of the
> *using* class. A `Doc` that `uses: { Tagged }`, where `Tagged` declares a
> `tag` variable, should read it as `self tag` (an accessor `Tagged` provides),
> not as a bare `tag`.

So write your mixins to expose their state through accessor methods — which is
good mixin discipline anyway — and the using class's methods reach that state
with a self-send:

```smalltalk
"-- mixin-ivar.st --"
Object subclass: #Tagged
  instanceVariableNames: 'tag'.

Tagged >> tag
  ^ tag.

Tagged >> setTag: t
  tag := t.
  ^ self.

Object subclass: #Doc
  instanceVariableNames: 'title'
  uses: { Tagged }.

Doc >> setTitle: t
  title := t.
  ^ self.

Doc >> describe
  ^ title , ' [' , self tag , ']'.

d := (Doc new setTitle: 'Q3 Report') setTag: 'urgent'.
d describe.
```

```bash
$ ./build/protost mixin-ivar.st
Q3 Report [urgent]
```

`Doc>>describe` reads its *own* variable `title` directly, but reaches the
mixin's `tag` through the accessor `self tag`. That is the robust pattern.

## 11.3 Runtime composition: `addBehavior:`

`uses:` bakes a class's parents in *at definition time*. protoST goes one step
further: you can compose a behaviour into a class **after it is defined**, while
the program runs, with no recompilation. The message is `addBehavior:`:

```smalltalk
"-- add-behavior.st --"
Object subclass: #Service
  instanceVariableNames: ''.

Service >> name
  ^ 'payment-service'.

Object subclass: #Logging
  instanceVariableNames: ''.

Logging >> log: aMessage
  ^ '[LOG] ' , aMessage.

"Compose the Logging behaviour into Service — at runtime."
Service addBehavior: Logging.

s := Service newChild.
s name , ' / ' , (s log: 'started').
```

```bash
$ ./build/protost add-behavior.st
payment-service / [LOG] started
```

`Service` was declared with no mention of `Logging`. The statement `Service
addBehavior: Logging` adds `Logging` as a further parent of `Service`, at
runtime. After it, a `Service` understands `log:` — a method it never declared
and was never compiled against.

`addBehavior:` composes freely with `uses:` (a class defined with mixins can be
given still more behaviour later) and may be called more than once. The added
behaviour is searched *after* the class's existing superclass and `uses:`
subtrees, consistent with the §11.2 resolution order. (`addParent:` is a
lower-level alias — the same operation named after the prototype mechanism.)

> **In Python** this is *monkey-patching* — assigning a method onto a class
> after the fact — and it is generally frowned upon as fragile. **In
> JavaScript** it is mutating a prototype. **In protoST** it is a *first-class,
> documented* operation: `addBehavior:` is a supported message, not a hack. It
> reflects the kernel's nature — a prototype's parent set is genuinely mutable
> — and it is the most direct demonstration of what protoST means by
> "prototype-based": a class's behaviour assembled incrementally, at runtime,
> from independent mixins.

### The one limitation: future instances only

There is a precise and important boundary on `addBehavior:`, and you must know
it:

> `addBehavior:` affects the class object and **every instance created *after*
> the call**. An instance created *before* the call does **not** gain the new
> behaviour.

```smalltalk
"-- d21.st --"
Object subclass: #Thing
  instanceVariableNames: ''.

Object subclass: #Bonus
  instanceVariableNames: ''.

Bonus >> bonus
  ^ 'bonus granted'.

earlyInstance := Thing newChild.        "created BEFORE addBehavior:"
Thing addBehavior: Bonus.
lateInstance := Thing newChild.         "created AFTER addBehavior:"

earlyResult := [ earlyInstance bonus ]
  on: Error
  do: [ :e | 'early instance: does not understand bonus' ].

earlyResult , ' || ' , (lateInstance bonus).
```

```bash
$ ./build/protost d21.st
early instance: does not understand bonus || bonus granted
```

`lateInstance`, created after `addBehavior:`, understands `bonus`.
`earlyInstance`, created before, does **not** — sending it `bonus` is a
`MessageNotUnderstood`.

The reason is in the kernel: protoCore freezes an object's *parent chain* into
the object at construction. `addBehavior:` necessarily produces a new chain,
and only *future* instances copy it. This is recorded as intentional deviation
D21 in `docs/STATUS.md`. (One subtlety: this limit applies only to new
*parents*. A method installed directly onto a class with `>>` *is* seen by
pre-existing instances — it is only new parents that pre-existing instances
miss.)

There is no `removeBehavior:` — protoCore's parent API offers no clean removal
of a baked-in parent, so it is out of scope.

> **Practical guidance.** Call `addBehavior:` *early* — during program setup,
> before you create the instances that need the composed behaviour. Used that
> way, the "future instances only" rule never bites: every instance you go on
> to create sees the full behaviour. The rule only surprises you if you mutate
> a class *after* its instances already exist.

## 11.4 Choosing between `uses:` and `addBehavior:`

Both compose behaviour from mixins; they differ in *when*:

- **`uses:`** — the parents are fixed in the class declaration. Use it when you
  know, at the time you write the class, which behaviours it needs. This is the
  common case, and the one to prefer.
- **`addBehavior:`** — the parent is added at runtime. Use it when the
  composition is genuinely dynamic — decided by configuration, by a plugin
  discovered at startup, by program logic. Call it during setup, before the
  affected instances exist.

If in doubt, use `uses:`. `addBehavior:` is the tool for the genuinely runtime
case — and it is what most vividly shows off the prototype kernel.

## 11.5 Summary

- protoST's object model is prototype-based; a prototype may have **several
  parents**, and that is what powers this chapter's two features.
- **`uses: { Mixin … }`** in a class declaration gives the class additional
  parents — real multiple inheritance. A *mixin* is just an ordinary class.
- Method resolution across parents is depth-first, left-to-right: primary
  superclass subtree first, then each `uses:` mixin in listed order; the
  diamond case resolves to the first match.
- Reach a **mixin's instance variables through accessor methods** (`self tag`),
  not by bare name in the using class — the robust pattern on the current
  build.
- **`addBehavior:`** composes a mixin into a class **at runtime**, with no
  recompilation. It affects the class and all instances created *afterwards* —
  intentional deviation D21 — so call it during setup. There is no
  `removeBehavior:`.
- Prefer `uses:` when the composition is known at write time; reach for
  `addBehavior:` only when it is genuinely dynamic.

---

Next: [Chapter 12 — Tooling](12-tooling.md)
