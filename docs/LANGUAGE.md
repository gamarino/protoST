# protoST Language Reference

> **Status.** This is the authoritative reference for the protoST language —
> Track 6, slice 1 of the project roadmap (`docs/ROADMAP.md`). It describes
> the language as it is *intended* to behave. Where the present implementation
> deviates from that intent, [§14 Known deviations](#14-known-deviations)
> summarises the delta and defers to the live tracker `docs/STATUS.md` for the
> current state of each item; the conformance suite is expected to have tests
> that fail on the open deviations, which is intentional.
>
> A second engineer, or a conformance-test author, should be able to read
> *only* this document and know how protoST is supposed to behave, without
> reading the implementation.
>
> Reference point: commit `27cfda2`, 334/334 tests passing. Phases F1–F8 plus
> Track 1 (non-local return, exceptions) and Track 2 (collections) are
> complete.

---

## Table of contents

1. [Overview](#1-overview)
2. [Lexical structure](#2-lexical-structure)
3. [Grammar](#3-grammar)
4. [The object model](#4-the-object-model)
5. [Messages and dispatch](#5-messages-and-dispatch)
6. [Blocks and closures](#6-blocks-and-closures)
7. [Non-local return](#7-non-local-return)
8. [Exceptions](#8-exceptions)
9. [Collections](#9-collections)
10. [Actors and futures](#10-actors-and-futures)
11. [Modules](#11-modules)
12. [Built-in classes and selectors](#12-built-in-classes-and-selectors)
13. [The CLI](#13-the-cli)
14. [Known deviations](#14-known-deviations)

---

## 1. Overview

protoST is an **actor-native Smalltalk runtime built on protoCore**. It is one
of three language runtimes (alongside protoJS and protoPython) that share a
single prototype-based kernel — protoCore — and it is the demonstration that
that kernel can host a message-passing object paradigm without flattening it.

protoST's distinguishing contribution is a **first-class embedded actor
model**: any object can be promoted to an actor with `asActor`, message sends
to an actor are asynchronous and return `Future`s, and a cooperative scheduler
runs many lightweight actors on a worker pool. The intended application domain
is **digital twins** — collections of finite state machines representing
real-world entities, exchanging events.

### 1.1 Design stance

protoST is a **Smalltalk-80 dialect**. It is "as close and as compliant as
reasonable" to Smalltalk-80, but **standard conformance is not the goal**: the
goal is a coherent, complete, well-tested language that shows off protoCore.
Where a non-standard feature exhibits something the core makes possible,
protoST may add it. Consequently this reference describes protoST on its own
terms, noting where it follows or departs from Smalltalk-80.

Notable deliberate departures from Smalltalk-80:

- **No image / no persistence.** protoST is strictly file-based. There is no
  `ChangeSet`, no `become:`, no world snapshot.
- **No classic metaclass tower.** "Class-side methods" exist but there is no
  separate `Metaclass` object and no `Class class class` recursion.
- **Prototype-based kernel.** A class is a prototype object; an instance is a
  child of that prototype. Inheritance is prototype-chain delegation.
- **Actors and futures are built in**, not a library.
- protoCore supports multiple parents, so protoST is not fundamentally
  single-inheritance, although the surface syntax exposes only single
  inheritance today.

### 1.2 A first example

```smalltalk
"-- a class, an instance method, and a top-level expression --"
Object subclass: #Counter instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

c := Counter new.
c initialize.
c increment.
c increment.
c value.        "evaluates to 2 — the value of the whole program"
```

When run as a script, the value of the **last top-level statement** is the
program's result.

---

## 2. Lexical structure

The lexer turns source text into a stream of tokens. Whitespace (spaces, tabs,
newlines) separates tokens and is otherwise insignificant.

### 2.1 Comments

A comment is text enclosed in **double quotes**:

```smalltalk
"this is a comment"
```

A comment may span multiple lines. A literal double-quote character inside a
comment is not supported (there is no escape); the comment ends at the first
closing `"`. Comments may appear anywhere whitespace may appear.

### 2.2 Identifiers

An identifier begins with a letter or `_` and continues with letters, digits,
or `_`:

```
identifier  ::=  (letter | '_') (letter | digit | '_')*
```

Identifiers are case-sensitive. By convention, class and global names are
capitalised; variables are lower-case. Three identifiers are **reserved
pseudo-variables**: `self`, `super`, `thisContext`. Three more are **literal
keywords**: `true`, `false`, `nil`.

### 2.3 Keyword tokens

An identifier **immediately followed by a colon** (no intervening whitespace)
is a *keyword token* — the building block of keyword messages:

```smalltalk
at:        put:        ifTrue:        instanceVariableNames:
```

The `:` is part of the token. `foo:` is a keyword token; `foo :` (with a
space) is the identifier `foo` followed by the colon token used for block
arguments. An identifier followed by `:=` is *not* a keyword token — it is an
identifier followed by the assignment operator.

### 2.4 Integer literals

An integer literal is a run of decimal digits:

```
42      0      1000000
```

There is no radix syntax (`16r1F`), no digit separators, and no exponent.

> **Negative numeric literals.** A `-` immediately followed by a digit, in
> operand/primary position (start of stream, or after a binary operator,
> keyword, `(`, `[`, `^`, `:=`, `.`, `;` or `>>`), is the sign of a negative
> numeric literal: `-1` and `-3.14` are literals, and `#(-1 -2 -3)` is a
> three-element array. A `-` that immediately follows an operand (a literal,
> identifier or closing bracket) is still the binary minus operator, so
> `a - 5` and `3 - 5` remain subtraction sends.

An integer literal that overflows a 64-bit signed range is a lexical error.
Small integers are represented inline (tagged); larger values promote to a
heap `LargeInteger` — the boundary is transparent to the program.

### 2.5 Float literals

A float literal is digits, a `.`, and more digits. **Both sides of the `.` are
mandatory** — `3.14` is a float, but `3.` is the integer `3` followed by a
statement terminator, and `.5` is not a float.

```
3.14      0.0      100.5
```

There is no exponent syntax.

### 2.6 Character literals

A character literal is `$` followed by exactly one character:

```
$a      $Z      $       $$
```

`$` followed by a space is the space character; `$$` is the dollar character.

### 2.7 String literals

A string literal is text enclosed in **single quotes**. A literal single quote
inside the string is written by doubling it:

```smalltalk
'hello'                 "the string: hello"
'it''s here'            "the string: it's here"
''                      "the empty string"
```

Strings may span multiple lines (a newline inside the quotes is part of the
string). There are no backslash escapes. An unterminated string is a lexical
error.

### 2.8 Symbol literals

A symbol literal is `#` followed by either an identifier-like name or an
operator. Symbols are interned (each distinct symbol value is a unique object).

```smalltalk
#foo            "an identifier symbol"
#at:put:        "a keyword-selector symbol — the chain of name: segments"
#+              "a binary-operator symbol"
#with:with:     "a multi-keyword selector symbol"
```

The keyword form chains: `#` then `name`, then any number of `:name` segments.
A binary-operator symbol takes one or two characters from the operator
alphabet `+ - * / = ~ < > & | @ ,`.

### 2.9 Array-literal syntax

Two bracketed forms produce arrays:

- `#( ... )` — a **literal array**. Its elements are *compile-time literals
  only*: integers (including negative literals), floats, strings, characters,
  and symbols. A **bare identifier inside `#( ... )` is treated as a symbol**
  (e.g. `#(foo bar)` is an array of the symbols `#foo` and `#bar`). A nested
  `#( ... )` — and, per standard Smalltalk, a bare `( ... )` group — inside a
  literal array is itself a nested literal sub-array (`#(1 #(2 3) 4)` has three
  elements; `#(#(1 2) #(3 4))` has two).
- `{ ... }` — a **dynamic array**. Its elements are arbitrary expressions
  separated by `.`; each is evaluated at runtime when the literal is reached.

```smalltalk
#(1 2 3)                 "literal array of three integers"
#($a 'str' #sym foo)     "char, string, symbol, and the symbol #foo"
{ 1 + 1. 2 * 2. x }      "dynamic array — elements computed at runtime"
{ }                      "the empty dynamic array"
#()                      "the empty literal array"
```

Both forms produce an `Array` (see [§9](#9-collections)).

### 2.10 Operators and punctuation

| Token | Meaning |
|-------|---------|
| `+ - * / = ~= < > <= >= & \| @ , ->` | binary operator selectors |
| `== ~~` | binary operators (identity / non-identity comparison) |
| `:=` | assignment |
| `^` | method return |
| `;` | cascade separator |
| `.` | statement terminator |
| `( )` | grouping |
| `[ ]` | block delimiters |
| `{ }` | dynamic-array delimiters |
| `#(` | literal-array opener |
| `\|` | block/method local-variable separator (also the binary operator) |
| `:` | block-argument prefix |
| `>>` | method-definition marker |

A binary operator is one or two characters drawn from the operator alphabet.
`->` produces an `Association` (see [§9.9](#99-association)). The token `\|`
is disambiguated by the parser: it is the locals separator at the start of a
block/method body and the binary operator elsewhere.

### 2.11 The `>>` method marker

`>>` is a distinct token used only in file-out method definitions:
`ClassName >> selector`. It is not a message selector.

---

## 3. Grammar

This section gives the concrete grammar of a `.st` program. The notation is
EBNF-like; `*` is zero-or-more, `?` is optional, `|` is alternation.

### 3.1 Top-level program

A `.st` file is a sequence of **top-level forms**:

```
program     ::=  topForm*
topForm     ::=  classDecl | methodDecl | statement '.'
```

Three top-level forms are recognised:

1. a **class declaration**,
2. a **method definition**,
3. a **top-level statement** terminated by `.`.

Top-level forms execute in source order when the module is loaded. Class
declarations and method definitions are themselves executed (they create the
class object and install the method); see [§4](#4-the-object-model).

### 3.2 Class declarations

```
classDecl  ::=  Identifier 'subclass:' SymbolLit
                  ( 'instanceVariableNames:' StringLit )?
                  ( 'classVariableNames:'    StringLit )?
                  '.'?
```

The leading `Identifier` is the **superclass** name; the symbol after
`subclass:` is the **new class name**. `instanceVariableNames:` takes a string
of space-separated instance-variable names.

```smalltalk
Object subclass: #Point
  instanceVariableNames: 'x y'.

Object subclass: #Counter
  instanceVariableNames: 'value'
  classVariableNames: ''.
```

The trailing `.` is optional. An empty `classVariableNames: ''` clause is a
documented no-op. A *non-empty* `classVariableNames:` clause is rejected with a
compile-time diagnostic: class variables are not yet implemented (tracked as
D19 in `docs/STATUS.md`), so the clause is no longer silently discarded.

### 3.3 Method definitions

```
methodDecl ::=  Identifier ('class')? '>>' selectorPattern localVars? statement*
selectorPattern ::=  Identifier                              "unary"
                  |  BinaryOp Identifier                     "binary"
                  |  ( Keyword Identifier )+                  "keyword"
localVars  ::=  '|' Identifier* '|'
```

A method definition names its class, the optional `class` marker for a
class-side method, the `>>` marker, the **selector pattern** (which fixes the
selector and the argument names), optional local variables, and the body
statements.

```smalltalk
Counter >> increment             "unary selector: increment"
  value := value + 1.

Point >> + aPoint                 "binary selector: +, one argument"
  ^ Point new setX: x + aPoint x setY: y + aPoint y.

Dictionary >> at: aKey put: aValue   "keyword selector: at:put:, two args"
  ...

Counter class >> startingAt: n    "class-side method"
  | c |
  c := self new.
  c setValue: n.
  ^ c.
```

A method body runs until the next top-level form begins (the parser detects
the start of another `>>` / `class` / `subclass:`). An explicit `^` return
**terminates the method body**: anything after the first top-level `^` is read
as a new top-level form. A method with no `^` returns `self` by default.

### 3.4 Statements

```
statement  ::=  '^' expression          "return"
             |  Identifier ':=' expression   "assignment"
             |  expression
```

Statements within a block or method body are separated by `.`. A trailing `.`
after the last statement is optional.

### 3.5 Expressions and message precedence

An expression is a receiver followed by zero or more message sends. There are
three message forms, with **strict precedence** from tightest to loosest:

1. **Unary** — `receiver selector` — a bare identifier selector. Highest
   precedence; chains left-to-right.
2. **Binary** — `receiver op argument` — an operator selector with one
   argument. Lower than unary; chains left-to-right (no operator-specific
   precedence — `2 + 3 * 4` is `(2 + 3) * 4` = 20).
3. **Keyword** — `receiver kw1: arg1 kw2: arg2 ...` — the selector is the
   concatenation of the keyword parts (`kw1:kw2:`). Lowest precedence; a
   keyword message takes the whole expression as far as it can.

```smalltalk
3 factorial + 4 factorial          "= (3 factorial) + (4 factorial)"
2 + 3 * 4                          "= (2 + 3) * 4 = 20"
anArray at: i + 1 put: x size      "= anArray at: (i + 1) put: (x size)"
```

Parentheses `( ... )` override precedence and contain a full expression.

```
expression   ::=  keywordSend cascadeTail?
keywordSend  ::=  binarySend ( Keyword binarySend )*
binarySend   ::=  unarySend ( BinaryOp unarySend )*
unarySend    ::=  primary Identifier*
primary      ::=  literal | Identifier | 'self' | 'super' | 'thisContext'
               |  '(' expression ')' | block | arrayLit | dynArrayLit
```

### 3.6 Assignment

```smalltalk
name := expression
```

Assignment binds `name` to the value of `expression`. The target may be a
method/block temporary, a method argument, an instance variable of the current
method's class, or (at module top level) a global. An assignment is itself an
expression whose value is the assigned value, so `a := b := 0` is legal.

### 3.7 Return

`^ expression` returns `expression` from the **enclosing method** — not merely
from the block it textually appears in. See [§7](#7-non-local-return). At
module top level, `^ expression` returns from the module.

### 3.8 Blocks

```
block      ::=  '[' blockArgs? localVars? statement* ']'
blockArgs  ::=  ( ':' Identifier )+ '|'
```

A block is a literal closure. It may declare arguments (`:name`, terminated
by `|`) and locals (`| name ... |`). The body is statements separated by `.`.
A block's value, when evaluated, is the value of its last statement (or `nil`
if empty).

```smalltalk
[ 42 ]                       "a zero-argument block"
[ :x | x + 1 ]               "one argument"
[ :a :b | a + b ]            "two arguments"
[ :x | | t | t := x * x. t ] "an argument and a local"
[ ]                          "empty block — evaluates to nil"
```

See [§6](#6-blocks-and-closures).

### 3.9 Cascades

A **cascade** sends several messages to the *same receiver*. After the first
message, each `;` introduces another message sent to the same receiver as the
first message:

```smalltalk
coll add: 1; add: 2; add: 3
```

Here `add: 1`, `add: 2`, and `add: 3` are all sent to `coll`. The value of a
cascade is the value of the **last** message in it. The receiver of the
cascade is the receiver of the first message (so `OrderedCollection new add: 1;
add: 2; yourself` cascades onto the new collection).

### 3.10 `thisContext`

`thisContext` is a reserved pseudo-variable that parses to its own node kind.
The reflective context protocol is not part of this slice; treat `thisContext`
as reserved but not yet meaningful.

---

## 4. The object model

### 4.1 Everything is an object

Every value in protoST is an object: integers, floats, characters, strings,
symbols, booleans, `nil`, blocks, collections, exceptions, futures, actors,
classes, and user instances. Every object responds to messages.

### 4.2 Prototypes, classes, and instances

protoST's object model is **prototype-based** (inherited from protoCore). A
class is an ordinary object acting as a Lieberman-style prototype. An instance
is a *child* of its class prototype: it delegates any attribute or method it
does not define itself to the prototype.

The built-in class hierarchy, bootstrapped at runtime start:

```
Object
  Number
    SmallInteger        (Integer-family — small values)
    LargeInteger        (Integer-family — large values)
    Float
  Boolean               (True / False)
  String
    Symbol
  Block
  UndefinedObject       (the class of nil)
  Actor
  Future
  Exception
    Error
    Warning
  Collection
    SequenceableCollection
      Array
      OrderedCollection
      Interval
    HashedCollection
      Set
      Bag
      Dictionary
  Association
```

User classes are children of `Object` (or of any other class) created with
`subclass:`.

### 4.3 Defining a class

```smalltalk
Object subclass: #Account
  instanceVariableNames: 'balance owner'.
```

This creates a new class object `Account`, a child of `Object`, and binds it
as a **global** under the name `Account`. Its instances carry two instance
variables, `balance` and `owner`. Executing the declaration is what creates
the class — class declarations are runtime forms.

### 4.4 Creating instances

A new instance is created by sending `new` (or its synonym `newChild`) to the
class:

```smalltalk
a := Account new.
```

`new` returns a fresh, mutable child of the class prototype. Its instance
variables start as `nil`.

> **`new` does not auto-call `initialize`.** Unlike many Smalltalks, `Account
> new` does *not* automatically send `initialize` to the new instance. If a
> class defines `initialize`, the caller must send it explicitly:
> `a := Account new. a initialize.` See [§14](#14-known-deviations).

### 4.5 Instance variables

Instance variables are named in the class declaration. Within an instance-side
method, an instance-variable name used as an expression reads that variable;
used as an assignment target it writes it. Instance variables are private to
the instance and not directly visible from outside (access them through
accessor methods).

```smalltalk
Account >> balance
  ^ balance.

Account >> deposit: amount
  balance := balance + amount.
```

An instance variable that has never been assigned reads as `nil`.

### 4.6 `self` and `super`

`self` is the receiver of the currently executing method. A self-send
(`self foo`) dispatches normally, starting the method lookup at the receiver's
prototype.

`super` is also the receiver, but a `super`-send starts method lookup at the
**superclass of the method's defining class** — not at the receiver's
prototype. This lets an override reuse the inherited behaviour:

```smalltalk
Object subclass: #Animal.
Animal >> describe ^ 'an animal'.

Animal subclass: #Dog.
Dog >> describe ^ (super describe) , ' that barks'.

d := Dog new.
d describe.        "evaluates to 'an animal that barks'"
```

`super` is only meaningful inside a method body.

### 4.7 Class-side methods

A method defined with the `class` marker — `ClassName class >> selector` — is
a **class-side method**, intended to be reachable from the class object itself
(e.g. for custom constructors) but not from its instances.

```smalltalk
Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.
```

> **Implementation note.** Class-side and instance-side protocols are disjoint:
> a `ClassName class >> selector` method is reachable from the class object but
> *not* from its instances — an instance sending a class-side selector gets a
> `MessageNotUnderstood`. Internally both still install onto the one class
> object; class-side methods carry a marker that the send-dispatch path honours
> when the receiver is an instance.

### 4.8 `printString`

Every object responds to `printString`, which returns a human-readable
`String`. The default `Object>>printString`:

- For a class object, returns the bare class name (`'Counter'`).
- For an instance, returns `'a ClassName'`, or `'an ClassName'` when the class
  name starts with a vowel (`'a Counter'`, `'an Account'`).

A class may override `printString` with its own method; ordinary inheritance
applies. `printNl` prints the receiver (its string form) followed by a newline
and returns the receiver.

### 4.9 Globals

The global namespace holds all class names and any name assigned at module top
level. A free identifier in an expression that is not a local, argument, or
instance variable resolves as a global. A failed global lookup is a runtime
error (`undefined global: X`).

---

## 5. Messages and dispatch

### 5.1 Sending a message

A message send names a *receiver*, a *selector*, and zero or more *arguments*.
Dispatch resolves the selector against the receiver:

1. The runtime looks up the selector on the receiver, walking the prototype
   chain from the receiver's own attributes up through its ancestors.
2. The first prototype that defines the selector wins. If it is a **primitive**
   (a C++-implemented operation, e.g. integer `+`), the primitive runs. If it
   is a **user method** (a compiled `.st` method), the method is invoked with
   the receiver bound to `self` and the arguments bound to the parameter names.
3. If the receiver's prototype is the actor prototype, the send is instead
   *enqueued* on the actor and a `Future` is returned (see [§10](#10-actors-and-futures)).
4. If no prototype defines the selector, the send is **doesNotUnderstand**.

### 5.2 `doesNotUnderstand`

Sending a selector that no prototype in the receiver's chain understands is a
**doesNotUnderstand** condition.

An unresolved selector signals a **`MessageNotUnderstood`** exception — a
subclass of `Error` — through the normal exception machinery. An
`on: Error do:` (or `on: MessageNotUnderstood do:`) handler catches it, and the
caught exception's `messageText` reads `doesNotUnderstand: <selector>`. With no
handler the run aborts (an actor rejects its `Future`; a script terminates with
an error message).

> The reflective `doesNotUnderstand:` user hook — a method the runtime sends to
> the receiver so it can intercept unknown sends — is not implemented; an
> unresolved send always signals `MessageNotUnderstood` directly.

### 5.3 `super` dispatch

A `super`-send (`super selector ...`) is resolved exactly as in [§5.1] except
that the lookup begins at the **first parent of the defining method's class**,
skipping any override on the receiver itself. The receiver bound to `self` in
the invoked method is still the original receiver.

### 5.4 Argument count

A send carries a fixed number of arguments determined by the selector form:
unary = 0, binary = 1, keyword = the number of keyword parts. The current
engine limits a single send to at most 8 arguments.

---

## 6. Blocks and closures

### 6.1 Block syntax

A block `[ :args | temps body ]` is a literal closure object — an instance of
`Block`. Evaluating a block *literal* produces the closure; it does not run the
body. The body runs only when the block is *evaluated* with `value`/`value:`/…

### 6.2 Evaluating a block

A block is evaluated by sending it a `value`-family message whose arity matches
the block's argument count:

| Selector | Block arity |
|----------|-------------|
| `value` | 0 |
| `value:` | 1 |
| `value:value:` | 2 |
| `value:value:value:` | 3 |
| `value:value:value:value:` | 4 |

```smalltalk
[ 42 ] value                          "=> 42"
[ :x | x * x ] value: 5               "=> 25"
[ :a :b | a + b ] value: 3 value: 4   "=> 7"
```

The value of a block is the value of its last statement, or `nil` if the block
is empty. Evaluating a block with the wrong number of arguments is a runtime
error.

### 6.3 Closures: variable capture

A block **captures** the variables of the scope it is textually written in:

- a block written inside a method sees that method's arguments, temporaries,
  `self`, and instance variables;
- a block written inside another block sees the outer block's variables;
- a block written at module top level sees module-level variables.

Capture is by *reference* for mutable variables: if a block writes a captured
variable, the enclosing method observes the change, and vice versa.

```smalltalk
Foo >> sumUpTo: n
  | total |
  total := 0.
  1 to: n do: [ :i | total := total + i ].   "the block mutates `total`"
  ^ total.
```

A block that outlives the method that created it keeps the captured variables
alive (a true closure).

`self` inside a block is the `self` of the enclosing method — a block does not
have its own receiver. `super` is likewise inherited.

> **Limitation — shadowing.** A block cannot declare a captured variable with
> the *same name* as a captured variable of the enclosing method (two distinct
> variables, same name, in nested scopes). The capture mechanism uses one flat
> per-method dictionary and cannot distinguish them. See
> [§14](#14-known-deviations).

### 6.4 Control flow with blocks

protoST has no built-in control-flow statements. Conditionals and loops are
**ordinary message sends** that take blocks as arguments.

**Conditionals** — sent to a boolean:

```smalltalk
(x > 0) ifTrue: [ 'positive' ]                  "=> 'positive' or nil"
(x > 0) ifFalse: [ 'non-positive' ]
```

`ifTrue:` evaluates its block argument and returns the result when the
receiver is `true`, otherwise returns `nil`. `ifFalse:` is the mirror image.

**Loops** — `whileTrue:` is sent to a block (the condition):

```smalltalk
| i |
i := 1.
[ i <= 10 ] whileTrue: [ i := i + 1 ].
```

The receiver block is the condition; while it evaluates to `true`, the
argument block is evaluated. `whileTrue:` returns `nil`.

**Numeric iteration** — `to:do:` and `to:by:do:` on a number iterate:

```smalltalk
1 to: 5 do: [ :i | sum := sum + i ].          "i takes 1,2,3,4,5"
10 to: 1 by: -1 do: [ :i | ... ].             "counts down"
```

**Conditional and boolean protocol on `Boolean`** — `ifTrue:`, `ifFalse:`,
`ifTrue:ifFalse:`, `ifFalse:ifTrue:`; the short-circuit combinators `and:` /
`or:` (the argument is a block, evaluated only when needed); the eager
combinators `&` / `|` / `xor:` (the argument is an already-evaluated boolean);
and `not`.

**Nil-test protocol on `Object`** — `isNil`, `notNil`, `ifNil:`, `ifNotNil:`,
`ifNil:ifNotNil:`. `nil` (the sole `UndefinedObject`) answers `isNil` → `true`;
every other object answers `isNil` → `false`. An `ifNotNil:` block may take the
receiver as an argument (`anObject ifNotNil: [ :x | … ]`) or take none.

**Block loop protocol** — `whileTrue:`, `whileFalse:`, `whileTrue`,
`whileFalse` and `repeat` (an unbounded loop, exited by a non-local return).

---

## 7. Non-local return

`^ expression` inside a block returns from the block's **home method** — the
method activation in which the block was *textually* created — abandoning any
intervening computation. This is standard Smalltalk semantics.

```smalltalk
Foo >> firstEven: aCollection
  aCollection do: [ :x | x isEven ifTrue: [ ^ x ] ].
  ^ nil.
```

Here `^ x` returns from `firstEven:`, abandoning the `do:` loop and the
trailing `^ nil`. The `^` targets the home method even from arbitrary block
nesting depth (a `^` in a block in a block in a method still returns from the
method).

A `^` written directly in a method body (not inside a block) is an ordinary
method return.

A block that *falls off its end* without a `^` simply returns its last value
to whoever evaluated it — that is a local block return, not a method return.

### 7.1 Dead home

If a block outlives its home method — the home method has already returned, and
the block (an escaped closure) is then evaluated with a `^` inside it — the
non-local return has no live method to target. This is the **dead-home**
condition.

This signals a catchable **`BlockCannotReturn`** exception — a subclass of
`Error` — through the normal exception machinery, so an `on: Error do:` (or
`on: BlockCannotReturn do:`) handler catches it; the caught exception's
`messageText` reads `non-local return: home method has already returned`. With
no handler the run aborts; inside an actor it rejects that actor's `Future`.

---

## 8. Exceptions

protoST provides a Smalltalk-style exception protocol: a class hierarchy,
signalling, protected blocks, and handler actions.

### 8.1 The exception hierarchy

```
Exception        (root; resumable)
  Error          (subclass; NOT resumable)
  Warning        (subclass; resumable)
```

- **`Exception`** — the root. Resumable by default.
- **`Error`** — a serious fault. **Not resumable** — calling `resume:` on an
  `Error` is itself an error.
- **`Warning`** — a non-fatal condition. Resumable.

Users may subclass any of these: `Error subclass: #AppError.` A subclass
instance *is* an instance of its superclass for handler-matching purposes (an
`AppError` is caught by `on: Error do:`).

### 8.2 Signalling

```smalltalk
Error signal.                 "signal a bare Error"
Error signal: 'disk full'.    "signal an Error with a message text"
anExceptionInstance signal.   "signal an existing instance"
```

`signal` builds (or takes) an exception instance and searches the active
handler stack — innermost first — for a handler whose guard class matches the
exception's class. The matching handler runs (see [§8.4]).

If **no handler matches**, the exception's *default action* runs:

- `Error` — aborts the current activation with its message text (an actor
  rejects its `Future`; a script terminates with the error).
- `Warning` — prints and resumes.
- `Exception` — resumes with `nil`.

An exception carries at least `messageText` (set by `signal:` or via
`messageText:`); read it with `messageText`.

### 8.3 Protected blocks: `on:do:`

`[ protected ] on: ExceptionClass do: [ :ex | handler ]` evaluates the
`protected` block; if it signals an exception matching `ExceptionClass`, the
`handler` block runs with the exception instance bound to `:ex`.

```smalltalk
[ self risky ]
  on: Error
  do: [ :ex | Transcript show: ex messageText. ex return: nil ].
```

- If `protected` completes normally, `on:do:` yields its value.
- If a matching exception is signalled, `on:do:` yields whatever the handler
  decides (see [§8.4]).
- A non-matching exception propagates outward to the next enclosing handler.
- `on:do:on:do:` registers two guard/handler pairs in one construct:
  `[ ... ] on: E1 do: [ ... ] on: E2 do: [ ... ]`.

While a handler block runs, its own handler entry (and any inner ones) are
disabled — a `signal` inside a handler is caught by an *outer* handler, never
by itself.

### 8.4 Handler actions

The handler block runs with the signalling computation still on the stack.
What the handler does determines whether — and how far — to unwind:

| Action | Effect |
|--------|--------|
| handler falls off its end | `on:do:` yields the handler block's last value; the protected computation after the `signal` is abandoned. |
| `ex return: v` | `on:do:` yields `v`; the protected computation is abandoned. |
| `ex resume: v` | `signal` *returns* `v`; the protected computation **continues** from the `signal` point. Only valid for a resumable exception. |
| `ex resume` | as `resume: nil`. |
| `ex retry` | the protected block is re-evaluated from the start. |
| `ex pass` | the handler search resumes *outward* — the next matching enclosing handler is tried; if none, the default action runs. |
| `ex outer` | intended: like `pass` but the search round-trips back. Currently an alias of `pass` (see [§14](#14-known-deviations)). |

```smalltalk
"resume: — continue past the signal"
[ Warning signal: 'low ink'. 'done' ]
  on: Warning
  do: [ :ex | ex resume: nil ].          "=> 'done'"

"retry — re-run the protected block"
[ self attempt ]
  on: Error
  do: [ :ex | ex retry ].
```

### 8.5 `ensure:` and `ifCurtailed:`

`ensure:` and `ifCurtailed:` register cleanup blocks that run during unwinding:

- `[ block ] ensure: [ cleanup ]` — `cleanup` runs **whether `block` completes
  normally or is abandoned** by an unwind.
- `[ block ] ifCurtailed: [ cleanup ]` — `cleanup` runs **only on an abnormal
  exit** (an unwind passing through), not on normal completion.

```smalltalk
[ file write: data ] ensure: [ file close ].
```

### 8.6 Resumability

Resumability is a class-derived property: `Exception` and `Warning` are
resumable, `Error` is not. A `resume:` on a non-resumable exception is itself
an error. An exception translated from a native C++ exception (see [§8.7]) is
forced non-resumable, because the native stack between its origin and the
catch is already gone.

### 8.7 Native exception translation

Code reached through native primitives or UMD-loaded modules may raise C++
exceptions. The runtime catches these at the native boundary and translates
them into non-resumable protoST `Error`s, so a UMD/native fault can be caught
by an ordinary `on: Error do:` handler.

---

## 9. Collections

protoST provides a Smalltalk collection hierarchy built on protoCore's
structural-sharing primitives.

```
Collection                    (abstract — the shared iteration protocol)
  SequenceableCollection      (abstract — ordered, integer-indexed)
    Array                     fixed-size, indexed
    OrderedCollection         growable, indexed
    Interval                  lazy arithmetic sequence
  HashedCollection            (abstract)
    Set                       deduplicating
    Bag                       counts duplicates
    Dictionary                key -> value map
```

`Association` (a key/value pair) is a direct child of `Object`, not a
collection.

### 9.1 Indexing

Sequenceable collections are **1-indexed**: `at: 1` is the first element.
`#(10 20 30) at: 2` evaluates to `20`. An out-of-range index signals an
`Error` catchable by `on:do:`.

### 9.2 The iteration protocol

These messages are defined on `Collection` and inherited by every collection:

| Selector | Meaning |
|----------|---------|
| `do:` | evaluate the block for each element; returns the receiver |
| `collect:` | a new collection of the block's results |
| `select:` | a new collection of elements for which the block is `true` |
| `reject:` | a new collection of elements for which the block is `false` |
| `detect:` | the first element for which the block is `true`; signals an `Error` if none |
| `detect:ifNone:` | as `detect:`, but evaluates the fallback block when none matches |
| `inject:into:` | fold: `inject: seed into: [ :acc :each | ... ]` |
| `do:separatedBy:` | `do:`, running the separator block between elements |
| `count:` | the number of elements satisfying the block |
| `anySatisfy:` | `true` if any element satisfies the block |
| `allSatisfy:` | `true` if every element satisfies the block |
| `,` | concatenation — a new collection of both operands' elements |
| `asArray` | an `Array` copy of the receiver |
| `size` | the element count |
| `isEmpty` / `notEmpty` | emptiness tests |
| `species` | the class used to build derived collections |

```smalltalk
#(1 2 3 4) size                                  "=> 4"
#(1 2 3) do: [ :e | sum := sum + e ].            "sum becomes 6"
(#(1 2 3 4) collect: [ :e | e * e ])             "=> an Array 1 4 9 16"
(#(1 2 3 4) select: [ :e | e isEven ])           "=> an Array 2 4"
(#(1 2 3 4) inject: 0 into: [ :a :e | a + e ])   "=> 10"
(#(3 1 4 1) detect: [ :e | e > 2 ])              "=> 3"
(#(1 2) , #(3 4))                                "=> an Array 1 2 3 4"
```

`collect:`, `select:`, `reject:` produce a collection of the receiver's
*species* (an `Array` from an `Array`, an `OrderedCollection` from an
`OrderedCollection`, a `Set` from a `Set`, a `Bag` from a `Bag`).

### 9.3 `Array`

A fixed-size, integer-indexed collection. Created from a literal (`#( ... )` or
`{ ... }`) or from class-side constructors:

| Operation | Meaning |
|-----------|---------|
| `Array new: n` | a new Array of `n` `nil`s |
| `Array withAll: aCollection` | a new Array copying another collection |
| `Array with: a` … `with:with:with:with:` | a new Array of the given elements |
| `at:` / `at:put:` | read/write a slot (`at:put:` returns the stored value) |
| `size`, `do:`, `isEmpty`, `notEmpty` | as the protocol |

An `Array`'s *size* is fixed; `at:put:` replaces an existing slot but does not
grow the array.

### 9.4 `OrderedCollection`

A growable, integer-indexed collection.

| Operation | Meaning |
|-----------|---------|
| `OrderedCollection new` | a new empty collection |
| `OrderedCollection withAll: aColl` | a new collection copying another |
| `add:` / `addLast:` | append an element (returns the argument) |
| `addFirst:` | prepend an element |
| `addAll:` | append every element of another collection |
| `removeFirst` / `removeLast` | remove and return an end element |
| `remove:` | remove a matching element; signals an `Error` if absent |
| `remove:ifAbsent:` | as `remove:`, with a fallback block |
| `at:` / `at:put:`, `first`, `last`, `size`, `do:` | as expected |

```smalltalk
| oc |
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
oc removeFirst.        "=> 1; oc now holds 2 3"
oc size.               "=> 2"
```

### 9.5 `Interval`

A **lazy** arithmetic sequence — it stores `start`, `stop`, and `step` and
computes elements on demand; it has no backing store. Created with `to:` /
`to:by:` on a number:

```smalltalk
1 to: 10            "the Interval 1,2,...,10"
1 to: 10 by: 2      "the Interval 1,3,5,7,9"
```

Supports `do:`, `at:`, `first`, `last`, `size`, `isEmpty`, `notEmpty` and the
inherited iteration protocol.

### 9.6 `Set`

A deduplicating hashed collection — adding an element already present is a
no-op.

| Operation | Meaning |
|-----------|---------|
| `Set new`, `Set withAll: aColl` | construction |
| `add:` | add (no-op if already present) |
| `remove:` / `remove:ifAbsent:` | remove an element |
| `includes:` | membership test |
| `size`, `do:` | as expected |

### 9.7 `Bag`

A counting hashed collection — it records how many times each element was
added.

| Operation | Meaning |
|-----------|---------|
| `Bag new`, `Bag withAll: aColl` | construction |
| `add:` | add one occurrence |
| `add:withOccurrences:` | add `n` occurrences |
| `remove:` / `remove:ifAbsent:` | drop one occurrence |
| `occurrencesOf:` | the count of an element |
| `includes:`, `size`, `do:` | `size` counts occurrences; `do:` visits each occurrence |

### 9.8 `Dictionary`

A map from arbitrary object keys to values.

| Operation | Meaning |
|-----------|---------|
| `Dictionary new` | a new empty dictionary |
| `at:put:` | store a key/value (returns the value) |
| `at:` | the value for a key; an absent key is an error |
| `at:ifAbsent:` | the value, or the fallback block's value if absent |
| `at:ifAbsentPut:` | the value, computing and storing the fallback if absent |
| `removeKey:` / `removeKey:ifAbsent:` | remove a key |
| `includesKey:` | key-presence test |
| `keys` / `values` / `associations` | collections of the parts |
| `keysDo:` / `valuesDo:` / `keysAndValuesDo:` / `associationsDo:` | iteration variants |

```smalltalk
| d |
d := Dictionary new.
d at: #one put: 1.
d at: #two put: 2.
d at: #one.                  "=> 1"
d at: #three ifAbsent: [ 0 ] "=> 0"
d includesKey: #two.         "=> true"
```

Keys may be symbols, strings, integers, or other objects.

### 9.9 `Association`

An `Association` is a key/value pair, produced by the `->` binary operator:

```smalltalk
#name -> 'Ada'        "an Association whose key is #name, value is 'Ada'"
```

It responds to `key`, `key:`, `value`, `value:`.

### 9.10 Mutability

protoCore primitive collections are immutable; a protoST collection is a
*mutable wrapper* over an immutable primitive. A mutating operation (`add:`,
`at:put:`, `removeFirst`, …) replaces the wrapped data with a new immutable
snapshot via structural sharing — copy-on-write, no full rewrite. The wrapper
object's identity is stable across mutations.

---

## 10. Actors and futures

protoST's concurrency model is the actor model. It is built in, not a library.

### 10.1 Promoting an object to an actor

`anObject asActor` returns an **actor proxy** wrapping `anObject`. The wrapped
object is unchanged.

```smalltalk
sensor := TempSensor new.
sensor initialize.
actor := sensor asActor.
```

The proxy is **fully transparent**: it forwards *every* message it receives to
the wrapped object asynchronously (see [§10.2](#102-sending-to-an-actor)) —
there is no exception, not even for introspection selectors. Sending
`printString` to the proxy is itself an asynchronous send: it returns a
`Future` that resolves to the *wrapped object's* `printString`, never a
synchronous string describing "an Actor". Because every send is forwarded,
there is no synchronous way to observe, from the proxy, that it is an actor at
all — that opacity is the point. To obtain the wrapped object's printable form,
`wait` on the future: `(actor printString) wait`.

### 10.2 Sending to an actor

A message sent to an actor proxy is **not run synchronously**. It is placed on
the actor's mailbox and a `Future` is returned **immediately** to the caller.
The actor's worker runs the message later.

```smalltalk
reading := actor read.       "reading is a Future, not the sensor value"
```

An actor processes its mailbox messages **one at a time** — at most one method
runs on a given actor at any moment (the single-method invariant). This is what
serialises access to the wrapped object's state; the programmer does not write
locks.

### 10.3 `Future`

A `Future` is the eventual result of an asynchronous send. It is `pending`
until the actor finishes, then `resolved` with a value or `rejected` with an
exception.

| Selector | Meaning |
|----------|---------|
| `wait` | block until settled; return the value, or re-raise the rejection |
| `thenDo:` | register a block to run with the value when resolved |
| `catch:` | register a block to run with the cause when rejected |
| `resolve:` | resolve the future with a value (settles waiters/callbacks) |
| `rejectWith:` | reject the future with a cause |

```smalltalk
f := actor compute.
f thenDo: [ :result | Transcript show: result printString ].
v := f wait.                 "v is the resolved value"
```

### 10.4 Cooperative suspension

When an actor's running method sends `wait` to a *pending* `Future`, the actor
**suspends cooperatively**: its worker is freed to run other actors, and the
suspended actor resumes (on some worker) once the future settles. The suspend
is transparent — the method continues from the `wait` point with the resolved
value.

This is how the fleet pattern scales: thousands of actors share a small worker
pool because a `wait` does not tie up a worker.

> A `wait` only yields when invoked from Smalltalk bytecode. A `wait` from the
> main thread (a script's top level, the REPL) instead **blocks the calling OS
> thread** on a condition variable — intentional, the main thread acts as a
> synchronous client of the actor world.

### 10.5 `self` inside an actor

Inside a method running on behalf of an actor, `self` is the **wrapped base
object**, not the actor proxy. A self-send is therefore an ordinary
synchronous dispatch — it does *not* re-enqueue on the actor and does *not*
acquire the actor lock. The actor boundary is crossed only by sending to the
proxy.

### 10.6 The synchronization boundary

The lock-equivalent belongs to the **actor proxy**, not the wrapped object. The
wrapped object has no implicit lock; its instance variables are plain storage.
Three consequences the programmer must honour:

1. **One actor per wrapped object.** Wrapping the same object in two proxies
   and driving both in parallel re-introduces unsynchronised access.
2. **A reference to the object that pre-dates `asActor` bypasses the lock.**
   Sending directly to the underlying object after promoting it runs on the
   caller's thread, unsynchronised against the actor's worker.
3. **An actor never reaches inside another actor's wrapped object directly** —
   cross-actor communication is exclusively by message send to the proxy.

### 10.7 Errors in an actor

An exception unhandled inside an actor method propagates to the worker loop,
which **rejects that message's `Future`** with the exception. The actor stays
alive and processes its next message. Partial mutations performed before the
raise are *not* rolled back — protoST has no transactional default.

---

## 11. Modules

### 11.1 File-to-module mapping

A `.st` file is a module. Loading a module executes its top-level forms in
order; the resulting module object exposes the top-level names it defined
(class names, primarily) as attributes. Names beginning with `_` are not
exported.

### 11.2 `Import from:`

`Import` is a global object; `Import from: aPath` loads the module identified
by `aPath` and returns the module object:

```smalltalk
m := Import from: 'counter_lib'.
c := m Counter new.
```

Module resolution goes through protoCore's **Unified Module Discovery (UMD)**.
A protoST module provider resolves `.st` files; because UMD is shared across
the three runtimes, `Import from:` can also resolve modules served by protoJS
or protoPython providers when they share a `ProtoSpace`. Imported modules are
cached — importing the same path twice yields the same module object.

### 11.3 Virtual environments (venv)

protoST ships a Python-style venv mechanism for isolating projects: a `.venv/`
directory with its own installed modules, configuration, and bytecode cache.
The runtime discovers a venv via the `STENV` environment variable, then by
walking up from the working directory for a `.venv/`, then a home venv, then
system defaults. See [§13](#13-the-cli) for the `venv` CLI commands.

> The venv directory layout and the `create`/`activate`/`info` subcommands are
> implemented. The `install`/`freeze` subcommands and the `stproject.toml`
> manifest from the design spec are not yet present.

---

## 12. Built-in classes and selectors

This section lists the bootstrap classes and the selectors each understands.
It is a reference snapshot of the current implementation.

### 12.1 `Object` (root)

| Selector | Meaning |
|----------|---------|
| `new` / `newChild` | a fresh mutable instance |
| `printString` | human-readable `String` |
| `printNl` | print the receiver followed by a newline; returns the receiver |
| `asActor` | wrap the receiver as an `Actor` |
| `->` | build an `Association` (`key -> value`) |
| `==` `~~` | identity / non-identity — same object, or not |
| `=` `~=` | equality / inequality; the default is identity, overridden to value-equality on `SmallInteger`, `String`, `Symbol`, `Boolean` and other value types |
| `isNil` `notNil` | nil test — `false` / `true` for every object except `nil` |
| `ifNil:` `ifNotNil:` `ifNil:ifNotNil:` | nil-conditional evaluation; an `ifNotNil:` block may take the receiver as an argument |
| `on:do:`, `on:do:on:do:`, `ensure:`, `ifCurtailed:` | exception protocol (these are bound on `Block`; see §12.7) |
| `sleep:` | sleep the current thread N ms (a test helper, not for production use) |

### 12.2 `Number`, `SmallInteger`, `LargeInteger`, `Float`

protoST has a full numeric tower. `SmallInteger`, `LargeInteger` and `Float`
all descend from `Number`, and arithmetic, comparison and the unary numeric
operations are bound **once on `Number`**, so every numeric kind understands
the same protocol:

| Selector | Meaning |
|----------|---------|
| `+` `-` `*` | arithmetic |
| `/` | division; `/` by zero signals a `ZeroDivide` error |
| `//` | integer (truncating) division |
| `\\` | modulo — the remainder |
| `<` `<=` `>` `>=` | ordered comparison → a boolean |
| `=` `~=` | equality / inequality → a boolean (value equality across the tower, so `2 = 2.0`) |
| `negated` | the receiver with its sign flipped |
| `abs` | the absolute value |
| `isEven` `isOdd` | integer parity (a non-integral `Float` is neither) |
| `printString` | the value's decimal digits — a `Float` always shows a fractional part (`4.0`), a `LargeInteger` shows its exact digits in full |

The arithmetic primitives delegate to protoCore's own `ProtoObject`
arithmetic, which gives the tower three properties for free:

- **Mixed-mode coercion.** An operation with one `Float` operand produces a
  `Float`: `1 + 2.5` → `3.5`, `2.5 + 1` → `3.5`, `1 / 2.0` → `0.5`.
- **Transparent overflow promotion.** An integer result that exceeds the
  56-bit inline `SmallInteger` range is automatically promoted to a heap
  arbitrary-precision `LargeInteger` and stays exact — a `whileTrue:` loop
  computing `25!` yields the exact `15511210043330985984000000`, not a
  wrapped value. The boundary is invisible to the program.
- **One protocol.** Because the primitives are bound on `Number`, a `Float`
  and a `LargeInteger` answer exactly the same selectors a `SmallInteger`
  does.

> **Division.** `/` between two integers is *truncating* integer division
> (`4 / 2` → `2`, `1 / 3` → `0`) — protoST has no `Fraction` type, so it
> follows protoCore's integer `/`. `//` is an explicit integer-division alias.
> If either operand is a `Float`, `/` is float division (`1 / 2.0` → `0.5`).

Iteration helpers are bound on `Number`:

| Selector | Meaning |
|----------|---------|
| `to:` | an `Interval` from receiver to the argument, step 1 |
| `to:by:` | an `Interval` with the given step |
| `to:do:` | iterate `receiver..stop`, evaluating the block per integer |
| `to:by:do:` | iterate with a step |

#### Mathematical protocol

The mathematical operations are also bound on `Number` (so every numeric kind
understands them) — they are idiomatic unary / keyword messages on a number,
always available with no `Import`. They are C++ primitives, the transcendentals
thin wrappers over `<cmath>` (libm).

| Selector | Meaning |
|----------|---------|
| `sqrt` | square root → a `Float` |
| `sin` `cos` `tan` | trigonometric functions (radians) → a `Float` |
| `arcSin` `arcCos` `arcTan` | inverse trigonometric functions → a `Float` |
| `ln` | natural logarithm → a `Float` |
| `exp` | `e` raised to the receiver → a `Float` |
| `log` | base-10 logarithm → a `Float` |
| `log:` | logarithm in the given base → a `Float` |
| `floor` `ceiling` `rounded` `truncated` | round to an **integer** (an integer receiver answers itself) |
| `sign` | `-1` / `0` / `1` |
| `squared` | `self * self` (an integer square promotes to `LargeInteger` if it overflows) |
| `reciprocal` | `1 / self`, always a `Float` |
| `isZero` | comparison with zero → a boolean |
| `min:` `max:` | the smaller / larger of receiver and argument |
| `between:and:` | inclusive range test `low <= self <= high` → a boolean |
| `asFloat` | the receiver as a `Float` |
| `asInteger` | the receiver as an integer (a `Float` is truncated toward zero) |
| `asCharacter` | the integer code point as a 1-character `String` (the inverse of `String>>asInteger`) |
| `even` `odd` | integer parity (aliases of `isEven` / `isOdd`) |
| `factorial` | `1 * 2 * ... * n` on a non-negative integer — exact, promotes to `LargeInteger` |
| `raisedTo:` | exponentiation — see below |
| `gcd:` `lcm:` | greatest common divisor / least common multiple of two integers |

Class-side **constants** are bound on `Float`: `Float pi`, `Float e`,
`Float infinity`, `Float nan`.

> **Exact exponentiation and factorial.** `raisedTo:` with a non-negative
> integer exponent, and `factorial`, are computed by exact repeated
> multiplication, so each intermediate product promotes to a `LargeInteger`
> the moment it leaves the 56-bit `SmallInteger` range — `2 raisedTo: 100` and
> `30 factorial` are exact arbitrary-precision integers, never an overflowed
> `double`. A `Float` exponent (or a negative integer exponent) routes through
> libm `pow` and answers a `Float`.

> **Domain errors.** A libm domain error — `(-1) sqrt`, `0 ln` — is **not**
> turned into a protoST `Error`; the IEEE-754 result (`nan` / `inf`) is let
> through, the same total contract libm offers. The math protocol therefore
> never raises a domain error. Genuinely invalid *arguments* still raise a
> catchable `Error`: `factorial` of a negative integer, `gcd:` of zero and
> zero, a non-numeric argument to `min:` / `raisedTo:` / etc.

### 12.3 `Boolean` (`True` / `False`)

| Selector | Meaning |
|----------|---------|
| `ifTrue:` | evaluate the block if the receiver is `true`, else `nil` |
| `ifFalse:` | evaluate the block if the receiver is `false`, else `nil` |
| `ifTrue:ifFalse:` `ifFalse:ifTrue:` | two-armed conditional; exactly one block runs |
| `and:` `or:` | short-circuit conjunction / disjunction; the argument is a block |
| `&` `\|` `xor:` | eager conjunction / disjunction / exclusive-or; the argument is a boolean |
| `not` | logical negation |
| `=` | value equality (identity for the tagged boolean immediates) |

### 12.4 `String` / `Symbol`

| Selector | Meaning |
|----------|---------|
| `,` | concatenation → a new `String` |
| `size` | character count (Unicode codepoints) |
| `at:` | the n-th character (1-based) as a 1-character `String`; an out-of-bounds index signals an `Error` |
| `asInteger` | the Unicode code point of the first character (`nil` for the empty string) |
| `=` `~=` | content equality / inequality |
| `printNl` | print followed by a newline |

> protoST has no distinct `Character` type — a character is a 1-character
> `String` — so `'abc' at: 2` answers the `String` `'b'`. `Number>>asCharacter`
> is the inverse of `String>>asInteger`: it answers the 1-character `String`
> for a Unicode code point.

### 12.5 `Block`

| Selector | Meaning |
|----------|---------|
| `value` … `value:value:value:value:` | evaluate with 0–4 arguments |
| `whileTrue:` | loop: while the receiver block is `true`, evaluate the argument |
| `whileFalse:` | loop: while the receiver block is `false`, evaluate the argument |
| `whileTrue` `whileFalse` | loop: re-evaluate the receiver while it stays `true` / `false` |
| `repeat` | loop forever — exited only by a non-local return |
| `on:do:`, `on:do:on:do:` | run as a protected block |
| `ensure:`, `ifCurtailed:` | run with a cleanup block |

### 12.6 `Future`

`wait`, `thenDo:`, `catch:`, `resolve:`, `rejectWith:` — see [§10.3](#103-future).

### 12.7 `Exception` / `Error` / `Warning`

| Selector | Meaning |
|----------|---------|
| `signal`, `signal:` | raise the exception |
| `messageText`, `messageText:` | read/set the message text |
| `return:` | handler action — yield a value from `on:do:` |
| `resume`, `resume:` | handler action — resume the protected computation |
| `retry` | handler action — re-run the protected block |
| `pass`, `outer` | handler action — continue the handler search outward |

### 12.8 Collections

See [§9](#9-collections) for the full per-class protocol. The shared iteration
protocol (`do:`, `collect:`, `select:`, `reject:`, `detect:`, `detect:ifNone:`,
`inject:into:`, `do:separatedBy:`, `count:`, `anySatisfy:`, `allSatisfy:`, `,`,
`asArray`, `size`, `isEmpty`, `notEmpty`, `species`) is bound on `Collection`.

### 12.9 `Import`

`Import from: aPathString` — load and return a module (see [§11](#11-modules)).

> **There is no `Transcript`.** Smalltalk-80's standard output stream object is
> not provided. Use `printNl` to print. See [§14](#14-known-deviations).

---

## 13. The CLI

The runtime executable is `protost`.

| Invocation | Effect |
|------------|--------|
| `protost script.st [args...]` | Run `script.st`; print the value of its last top-level statement. |
| `protost -e '<expr>'` | Evaluate the expression and print the result. |
| `protost -i` | Start the interactive REPL. |
| `protost -d script.st` | Run the script under the CLI debugger. |
| `protost --dap` | Run the Debug Adapter Protocol server over stdin/stdout. |
| `protost --dump-ast script.st` | Parse and print the AST (development aid). |
| `protost venv create [path]` | Create a venv (default `.venv`). |
| `protost venv activate [path]` | Print the shell snippet to source. |
| `protost venv info` | Show the active venv. |
| `protost --help` / `--version` | Usage / version. |

### 13.1 The REPL

`protost -i` starts a read-eval-print loop. It auto-detects incomplete input
(unbalanced brackets, an unfinished multi-line method) and keeps reading at a
continuation prompt. Each result is printed. Meta-commands begin with `:` —
`:help` (`:h`) and `:quit` (`:q`) are supported; `Ctrl-D` also exits.

> The richer meta-command set from the design spec (`:load`, `:reload`,
> `:edit`, `:time`, `:doc`) is not implemented. Bytecode compilation
> (`protost compile`) is not implemented and is no longer advertised in the
> usage text. `main:` selector auto-invocation is not implemented.
> See [§14](#14-known-deviations).

### 13.2 Single runtime per process

A protoST runtime must be the **only** `STRuntime` in its process. Constructing
a second `STRuntime` corrupts symbol interning (see [§14](#14-known-deviations)).
The CLI always constructs exactly one.

---

## 14. Known deviations

This section summarises every place where the current implementation does not
match the behaviour described in the main text, **split into deviations that
are deliberate and deviations that are not**.

> **`docs/STATUS.md` is the live tracker.** This section is a stable summary;
> the *current* state of each item — still open, fixed, with repro and
> severity, and the fixing commit when closed — lives in `docs/STATUS.md`.
> When in doubt about whether an item below is still true, consult
> `docs/STATUS.md`, which is verified against the build and updated with every
> change. The conformance suite is expected to have tests that fail on the
> open deviations — that is intentional; the failures surface the bugs.

The id scheme (`D1..D18`) is shared with `docs/STATUS.md`. Items D6, D11, D19
and D20 are described in `docs/STATUS.md`: D6 is now closed (not reproducible),
D11 (`Float` / mixed-mode arithmetic) and D20 (`LargeInteger` arithmetic and
overflow promotion) are now closed (the numeric tower — see §12.2), and D19
(class variables) is tracked there as a not-yet-implemented feature.

### 14.1 Intentional deviations

These are deliberate design decisions. protoST diverges from standard
Smalltalk here *on purpose*; they are not bugs and they stay, documented with
their rationale. See `docs/STATUS.md` § *Intentional deviations* for the
canonical list.

- **D2 — single `STRuntime` per process.** A second `STRuntime` corrupts
  symbol interning, because protoCore's symbol caches are per-`ProtoSpace`
  C++ statics. protoST adopts "one runtime per process" as its operating
  contract rather than working around it; the CLI always constructs exactly
  one. *Affects:* [§13.2](#132-single-runtime-per-process).
- **D4 — `new` does not auto-invoke `initialize`.** `ClassName new` returns a
  raw instance; the caller sends `initialize` explicitly. This is a deliberate
  MVP semantics choice — `new` is the raw allocator. Standard Smalltalk-80
  defines `new` as `super new initialize`; protoST may align later.
  *Affects:* [§4.4](#44-creating-instances).
- **D7 — `outer` is an alias of `pass`.** An MVP simplification of the handler
  protocol. Strict `outer` semantics (run the enclosing handler, then return
  to the inner one) require resumable handler re-entry that is not built;
  `pass` is the shipped behaviour. *Affects:* [§8.4](#84-handler-actions).
- **D12 — no `main:` auto-invocation.** A script is simply its top-level forms
  run in order; the printed value is the last top-level statement. protoST
  scripts deliberately have no distinguished entry point.
  *Affects:* [§13](#13-the-cli).

### 14.2 Known bugs and not-yet-implemented features

These deviations are *not* deliberate: either something is broken (a bug) or a
planned feature is simply absent (not yet implemented). `docs/STATUS.md` is the
authoritative tracker — it carries the repro, severity, owning roadmap track,
and (once fixed) the closing commit for each.

**Bugs** — broken behaviour that contradicts the language's own intent or
examples:

> _No open bugs. The last open bugs — D3, D5, D8 — were fixed in `MNT-b2`
> (see below)._

> **Fixed (commit `MNT-b1`).** D1 (negative numeric literals), D13 (the CLI no
> longer advertises an unimplemented `compile` subcommand), D15
> (`classVariableNames:` now emits a compile-time diagnostic instead of being
> silently discarded), D16 (nested literal arrays parse) and D18 (`==`/`~~`
> bound on `Object`; `=`/`~=` universal with value-equality overrides) are
> resolved — see `docs/STATUS.md` *Closed items*.

> **Fixed (commit `MNT-b2`).** D3 (an unresolved selector signals a catchable
> `MessageNotUnderstood`, a subclass of `Error`), D5 (class-side methods are
> isolated from instances — a `ClassName class >> sel` method is no longer
> reachable from an instance) and D8 (a `^` in a block whose home method has
> already returned signals a catchable `BlockCannotReturn`, a subclass of
> `Error`) are resolved — see `docs/STATUS.md` *Closed items*.

> **Fixed (commit `MNT-c`).** D11 (`Float` and mixed-mode arithmetic) and D20
> (`LargeInteger` arithmetic with transparent overflow promotion) are resolved
> — the numeric tower now works (see §12.2). The arithmetic primitives delegate
> to protoCore's own promoting / coercing `ProtoObject` arithmetic and are
> bound on the shared `Number` prototype — see `docs/STATUS.md` *Closed items*.

**Not yet implemented** — planned features absent today (owning roadmap track
noted in `docs/STATUS.md`):

- **D10 — no `Transcript`.** The standard output-stream object is not
  provided; use `printNl`. *Affects:* [§12.9](#129-import).
- **D14 — REPL meta-commands limited to `:help` / `:quit`.** `:load`,
  `:reload`, `:edit`, `:time`, `:doc` are not implemented.
  *Affects:* [§13.1](#131-the-repl).
- **D17 — `thisContext` is reserved but inert.** It parses to its own node but
  the reflective context protocol is not built.
  *Affects:* [§3.10](#310-thiscontext).
- **D19 — class variables not implemented.** The runtime feature behind D15: a
  per-class shared variable visible to all instances and class-side methods.
  *Affects:* [§3.2](#32-class-declarations).

---

*End of the protoST Language Reference.*
