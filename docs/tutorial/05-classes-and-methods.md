# Chapter 5 — Classes, objects, methods

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 4](04-blocks.md) · Next: [Chapter 6 — Non-local return](06-non-local-return.md)

---

So far you have sent messages to objects the language gave you — integers,
strings, arrays, booleans. This chapter shows you how to define your *own*
objects: how to declare a class, give it state, give it behaviour, and weave
classes into a hierarchy.

## 5.1 Declaring a class

A class is declared by sending the message `subclass:instanceVariableNames:` to
an existing class — almost always `Object`, the root of the hierarchy:

```smalltalk
Object subclass: #Account
  instanceVariableNames: 'balance owner'.
```

Read it as the message send it is: receiver `Object`, selector
`subclass:instanceVariableNames:`, first argument the symbol `#Account` (the
new class's name), second argument the string `'balance owner'` (its instance
variables, space-separated). Executing this statement *creates* the class — a
class declaration is a runtime form, not a compile-time directive — and binds
the new class as a global named `Account`.

A class with no instance variables can omit the second keyword entirely:

```smalltalk
Object subclass: #Animal.
```

> **In Python** a class is `class Account: …`, a block of code under a header.
> **In JavaScript** it is `class Account { … }`. **In protoST** there is no
> class *block* — the declaration is a single message send, and the methods are
> added afterwards, one statement at a time (§5.3). The class is not a lexical
> container; it is an object you keep sending messages to.

> **Smalltalker note.** This is the familiar `subclass:instanceVariableNames:`
> message, and the file-out style of one declaration plus separate method
> definitions will be second nature. What is *not* here: no `category:`, no
> `poolDictionaries:`, and a non-empty `classVariableNames:` clause is rejected
> with a diagnostic rather than honoured — class variables are not yet
> implemented. [Chapter 14](14-for-the-smalltalk-programmer.md) catalogues the
> object-model differences in full.

## 5.2 Creating instances

An instance is made by sending `new` (or its exact synonym `newChild`) to the
class:

```smalltalk
a := Account new.
```

`new` returns a fresh, mutable object whose instance variables all start as
`nil`. That is *all* `new` does.

> **`new` does not call `initialize`.** This is a deliberate and important
> departure from most Smalltalks, and from Python's `__init__` / JavaScript's
> `constructor`. In protoST, `Account new` gives you a raw instance with `nil`
> fields. If the class defines an `initialize` method, **the caller must send
> it explicitly**:
>
> ```smalltalk
> a := Account new.
> a initialize.
> ```
>
> `docs/STATUS.md` records this as intentional deviation D4. The idiomatic
> protoST way to hide the two-step is a *class-side constructor* (§5.7) that
> does the `new` and the `initialize` for you.

## 5.3 Defining methods with `>>`

A method is defined with the `>>` marker: the class name, `>>`, the selector
pattern, then the body. Method definitions are top-level forms, written after
the class declaration:

```smalltalk
Account >> balance
  ^ balance.
```

`Account >> balance` says "install on class `Account` a method whose selector
is `balance`". The body `^ balance` returns the instance variable `balance`.

The selector pattern follows the three message forms from
[Chapter 2](02-objects-and-messages.md):

```smalltalk
"-- a unary method: selector is `balance`, no arguments --"
Account >> balance
  ^ balance.

"-- a keyword method: selector is `deposit:`, one argument named `amount` --"
Account >> deposit: amount
  balance := balance + amount.

"-- a binary method: selector is `+`, one argument named `other` --"
Account >> + other
  ^ balance + other balance.

"-- a multi-keyword method: selector is `from:to:`, two arguments --"
Account >> transfer: amount to: other
  self withdraw: amount.
  other deposit: amount.
```

The argument *names* in the pattern become the method's parameters, visible
throughout the body.

> **In Python** a method is `def deposit(self, amount): …` — `self` is an
> explicit first parameter. **In protoST** `self` is *implicit*: you never
> declare it, it is always the receiver. And the method name is not a plain
> identifier — for `deposit:` the selector literally *is* `deposit:`, colon
> included, and for `transfer:to:` the selector is the two keyword parts woven
> through the argument list.

### The `^`-terminator caveat

There is one practical rule about method bodies that the language reference
states but does not stress, and getting it wrong produces baffling errors:

> A method body runs until the parser sees the next top-level form (`>>`,
> `subclass:`, …) **or** the first `^` return. The first top-level `^`
> *terminates* the method body. A method whose last statement is **not** a
> `^` therefore risks "absorbing" the lines that follow it as part of its
> body.

In practice the safe, universal habit is: **end every method with an explicit
`^`** — `^ self` if the method has nothing else to return. Throughout this
tutorial methods end with `^ self` or `^ someValue` for exactly this reason.

```smalltalk
Account >> deposit: amount
  balance := balance + amount.
  ^ self.
```

`^ self` returns the receiver, which is the conventional "I have nothing
meaningful to return" answer and also makes the method chainable.

## 5.4 Instance variables, `self`

An instance variable named in the class declaration is private storage that
each instance carries. Inside an instance-side method, the variable's name used
as an expression *reads* it, and used as an assignment target *writes* it:

```smalltalk
Account >> deposit: amount
  balance := balance + amount.    "read balance, then write it"
  ^ self.
```

Instance variables are not visible from outside the object — there is no
`account.balance` access. The only way in is a message. To expose `balance`,
write an accessor method (`Account >> balance ^ balance.`); to allow updates,
write a setter (`Account >> balance: n  balance := n. ^ self.`).

`self` is the receiver of the currently running method. A *self-send*,
`self someMessage`, sends a message to the very object the method is running
on — dispatched normally, starting the method lookup at the receiver:

```smalltalk
Account >> transfer: amount to: other
  self withdraw: amount.       "self-send: re-enter this object via `withdraw:`"
  other deposit: amount.
  ^ self.
```

> **In Python/JS** `self`/`this` is the receiver too — but Python writes it as
> an explicit parameter and JavaScript's `this` is notoriously
> context-dependent. **In protoST** `self` is simple and unambiguous: it is
> always the object the method was sent to, never anything else, and you never
> declare it.

A complete, runnable class:

```smalltalk
"-- account.st --"
Object subclass: #Account
  instanceVariableNames: 'balance'.

Account >> initialize
  balance := 0.
  ^ self.

Account >> deposit: amount
  balance := balance + amount.
  ^ self.

Account >> withdraw: amount
  balance := balance - amount.
  ^ self.

Account >> balance
  ^ balance.

a := Account new.
a initialize.
a deposit: 100.
a deposit: 50.
a withdraw: 30.
a balance.
```

```bash
$ ./build/protost account.st
120
```

## 5.5 Inheritance and `super`

A class declared as a subclass of another *inherits* its methods. An instance
of the subclass understands every message the superclass defines, plus any the
subclass adds, plus any it overrides.

```smalltalk
"-- inherit.st --"
Object subclass: #Animal
  instanceVariableNames: ''.

Animal >> describe
  ^ 'an animal'.

Animal >> legs
  ^ 4.

Animal subclass: #Dog
  instanceVariableNames: ''.

Dog >> describe
  ^ 'a dog'.

d := Dog new.
{ d describe. d legs }.
```

```bash
$ ./build/protost inherit.st
an Array
```

`d describe` answers `'a dog'` — `Dog` *overrides* `describe`. `d legs` answers
`4` — `Dog` inherits `legs` unchanged from `Animal`. (The script's last
statement builds a two-element dynamic array; printing a collection shows its
class, `an Array` — [Chapter 8](08-collections.md) explains how to inspect
contents.)

When an override needs to *extend* rather than replace the inherited behaviour,
`super` is the tool. A `super`-send is sent to the same receiver as `self`, but
the method lookup starts at the **superclass of the class that defines the
running method** — skipping the override:

```smalltalk
"-- super.st --"
Object subclass: #Animal
  instanceVariableNames: ''.

Animal >> describe
  ^ 'an animal'.

Animal subclass: #Dog
  instanceVariableNames: ''.

Dog >> describe
  ^ (super describe) , ' that barks'.

(Dog new) describe.
```

```bash
$ ./build/protost super.st
an animal that barks
```

`Dog>>describe` calls `super describe`, which runs `Animal`'s version
(`'an animal'`), then appends `' that barks'`. Without `super`, writing
`self describe` here would call `Dog>>describe` again — infinite recursion.

> **In Python** this is `super().describe()`; in **JavaScript**,
> `super.describe()`. **In protoST** it is `super describe` — the same idea,
> "run the inherited version of this method". One subtlety worth knowing:
> protoST resolves the defining class by walking the receiver's actual
> prototype chain *by object identity*, not by re-looking-up the class name.
> This makes `super` correct even when the defining class is not a top-level
> global — for instance a class imported from a module and subclassed locally
> ([Chapter 9](09-standard-library.md)).

## 5.6 Defining operators

Because a binary operator is just a message ([Chapter 2](02-objects-and-messages.md)),
you define `+`, `<`, `=`, `,` and the rest on your own class exactly the way you
define any other method — there is no separate "operator overloading" feature,
because there were never any operators to overload:

```smalltalk
"-- vector.st --"
Object subclass: #Vec2
  instanceVariableNames: 'x y'.

Vec2 >> setX: ax y: ay
  x := ax.
  y := ay.
  ^ self.

Vec2 >> x  ^ x.
Vec2 >> y  ^ y.

Vec2 >> + other
  ^ Vec2 new setX: x + other x y: y + other y.

Vec2 >> printString
  ^ '(' , x printString , ', ' , y printString , ')'.

v := (Vec2 new setX: 1 y: 2) + (Vec2 new setX: 3 y: 4).
v printString.
```

```bash
$ ./build/protost vector.st
(4, 6)
```

`Vec2 >> + other` defines the binary selector `+` on `Vec2`. Now `v1 + v2` —
an ordinary binary send — adds two vectors. The example also overrides
`printString`: every object answers `printString`, and a class is free to
replace the default (`'a Vec2'`) with something meaningful.

> **In Python** you would define `__add__` and `__repr__`; in **JavaScript**
> there is no operator overloading at all and you would write a `.add()`
> method. **In protoST** `+` is *already* a method name, so `Vec2 >> + other`
> is not a special mechanism — it is the same `>>` you used for `deposit:`.

## 5.7 Class-side methods

Sometimes you want a method on the *class itself* rather than on its instances
— most often a constructor that builds and initialises an instance in one step,
hiding the `new` / `initialize` two-step from callers. Such a method is defined
with the `class` marker between the class name and `>>`:

```smalltalk
"-- class-side.st --"
Object subclass: #Counter
  instanceVariableNames: 'value'.

Counter >> setValue: n
  value := n.
  ^ self.

Counter >> value
  ^ value.

Counter >> increment
  value := value + 1.
  ^ self.

"-- a class-side constructor: a method on the Counter class object --"
Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.

c := Counter startingAt: 10.
c increment.
c value.
```

```bash
$ ./build/protost class-side.st
11
```

`Counter class >> startingAt: n` installs `startingAt:` on the *class object*
`Counter`. Inside it, `self` is the class (so `self new` makes a fresh
instance). Callers write `Counter startingAt: 10` and never see the `new` /
`setValue:` mechanics.

Class-side and instance-side protocols are kept disjoint: a class-side selector
is reachable from the class object but **not** from an instance — sending
`startingAt:` to a `Counter` *instance* is a `MessageNotUnderstood`. This is
deliberate (it is how `new` itself stays a class-only message).

> **In Python** this is a `@classmethod` or `@staticmethod`. **In JavaScript**
> it is a `static` method. **In protoST** it is the `class` marker on the
> method definition. The use is the same — alternative constructors and
> class-level utilities — and the idiom is the same: a class-side constructor
> that does `new` plus `initialize` is the standard cure for protoST's
> non-auto-`initialize` rule (§5.2). The standard-library modules use exactly
> this pattern (`ReadStream class >> on:`, `Random class >> seed:`).

## 5.8 Prototypes under the hood

protoST's object model is, underneath, *prototype-based* — inherited from the
protoCore kernel. A "class" is an ordinary object acting as a prototype; an
"instance" is a *child* of that prototype that delegates any message it does
not define itself up the chain to its parent. "Inheritance" is prototype-chain
delegation; `subclass:` makes a new prototype whose parent is the receiver.

You do not need this to write protoST — everything in this chapter presents as
classic class-based OO and works that way. But it explains the powerful
extensions [Chapter 11](11-advanced-object-model.md) covers: multiple parents
via `uses:`, and composing behaviour into a class at runtime with
`addBehavior:`. Those are not bolted onto a class system — they are the
prototype kernel showing through.

## 5.9 Summary

- A class is declared by sending `subclass:instanceVariableNames:` to an
  existing class (usually `Object`). The declaration runs at load time and
  binds the class as a global.
- `new` (or `newChild`) makes a raw instance with `nil` fields. It does **not**
  call `initialize` — send `initialize` yourself, or provide a class-side
  constructor.
- Methods are defined with `ClassName >> selectorPattern`. End every method
  body with an explicit `^` (use `^ self`) to avoid the body absorbing the
  lines that follow it.
- Instance variables are private; expose them with accessor methods. `self` is
  the (implicit) receiver; a self-send re-dispatches on it.
- A subclass inherits, adds, and overrides. `super` runs the inherited version
  of an overridden method.
- Binary operators are ordinary methods — define `+`, `<`, `,` with `>>` like
  any other selector.
- Class-side methods (the `class` marker) live on the class object — the home
  of constructors and class utilities.

---

Next: [Chapter 6 — Non-local return](06-non-local-return.md)
