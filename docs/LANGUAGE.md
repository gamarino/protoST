# protoST Language Reference

> **Status.** This is the authoritative reference for the protoST language —
> Track 6, slice 1 of the project roadmap (`docs/ROADMAP.md`). It describes
> the language as it is *intended* to behave. Where the present implementation
> deviates from that intent, [§14 Known deviations](#14-known-deviations)
> summarises the delta and defers to the live tracker `docs/STATUS.md` for the
> current state of each item. The examples in this document are run against
> the current build by `tests/docs/run_doc_snippets.py`.
>
> A second engineer, or a conformance-test author, should be able to read
> *only* this document and know how protoST is supposed to behave, without
> reading the implementation.
>
> Originally written at commit `27cfda2`, when Phases F1–F8 plus Track 1
> (non-local return, exceptions) and Track 2 (collections) were complete, and
> extended as later features landed. For the current test count and the state
> of each item, see `docs/STATUS.md`.

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

protoST is a **Smalltalk-syntax, actor-native runtime built on protoCore**: a
demonstrator of that kernel and a base for digital twins. It is not a
Smalltalk-80 implementation and not a replacement for an image environment.
It is one of several language runtimes (protoPython, protoJS, protoScala,
protoClojure among them) that share the prototype-based kernel protoCore.

protoST's distinguishing contribution is a **first-class embedded actor
model**: any object can be promoted to an actor with `asActor`, message sends
to an actor are asynchronous and return `Future`s, and a cooperative scheduler
runs many lightweight actors on a worker pool. The intended application domain
is **digital twins** — collections of finite state machines representing
real-world entities, exchanging events.

### 1.1 Design stance

protoST follows Smalltalk-80 syntax and semantics "as closely as reasonable",
but **standard conformance is not the goal**: the goal is a coherent,
well-tested language that shows what protoCore makes possible.
Where a non-standard feature exhibits something the core makes possible,
protoST may add it. Consequently this reference describes protoST on its own
terms, noting where it follows or departs from Smalltalk-80.

Notable deliberate departures from Smalltalk-80:

- **No image / no persistence.** protoST is strictly file-based. There is no
  `ChangeSet`, no `become:`, no world snapshot.
- **A thin metaclass.** Class-side methods, class variables and
  class-instance variables exist, and `3 class class` answers
  `SmallInteger class`, but there is no `Metaclass` / `ClassDescription`
  hierarchy to program against.
- **Prototype-based kernel.** A class is a prototype object; an instance is a
  child of that prototype. Inheritance is prototype-chain delegation.
- **Actors and futures are built in**, not a library.
- protoCore supports multiple parents; protoST exposes this through the
  `uses:` mixin clause (§4.11) — a class may inherit from several superclasses
  / mixins — and through `addBehavior:` (§4.12), which composes a behaviour
  into a class at runtime with no recompilation.

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

c := Counter new.     "new sends initialize, so value starts at 0"
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
42      0      1000000      16r1F      2r1010      1e10
```

A radix prefix `base r` gives the digits in another base (`16r1F` is 31,
`2r1010` is 10), and an exponent suffix `e` multiplies by a power of ten
(`1e10` is the integer 10000000000). There are no digit separators
(`1_000` is not a number).

> **Negative numeric literals.** A `-` immediately followed by a digit, in
> operand/primary position (start of stream, or after a binary operator,
> keyword, `(`, `[`, `^`, `:=`, `.`, `;` or `>>`), is the sign of a negative
> numeric literal: `-1` and `-3.14` are literals, and `#(-1 -2 -3)` is a
> three-element array. A `-` that immediately follows an operand (a literal,
> identifier or closing bracket) is still the binary minus operator, so
> `a - 5` and `3 - 5` remain subtraction sends.

An integer literal of any size is accepted. Small integers are represented
inline (tagged); larger values are a heap `LargeInteger` — the boundary is
transparent to the program.

### 2.5 Float literals

A float literal is digits, a `.`, and more digits. **Both sides of the `.` are
mandatory** — `3.14` is a float, but `3.` is the integer `3` followed by a
statement terminator, and `.5` is not a float.

```
3.14      0.0      100.5      1.0e16      2.5e-3
```

An exponent suffix `e` (optionally negative) scales the value by a power of
ten: `2.5e-3` is `0.0025`.

### 2.6 Character literals

A character literal is `$` followed by exactly one character:

```
$a      $Z      $       $$
```

`$` followed by a space is the space character; `$$` is the dollar character.
A character literal is an instance of `Character` (§12.4).

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
A symbol shorter than 8 bytes is represented exactly like the equal string,
so `#foo == 'foo'` is true and `#foo printString` is `'foo'`; longer symbols
are distinct objects (D35, see §14).

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
                  ( 'uses:'                  Expression )?
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

The trailing `.` is optional. `classVariableNames:` takes a string of
space-separated **class-variable names** — per-class shared storage,
initialised to `nil` at class-decl time and visible from every instance
(including instances of subclasses) via the same prototype-chain attribute
walk that resolves inst vars. The clause is honoured; an empty
`classVariableNames: ''` declares no names. Example:

```smalltalk
Object subclass: #Counter
  instanceVariableNames: 'value'
  classVariableNames: 'tally'.

Counter class >> initTally  tally := 0.
Counter class >> bump       tally := tally + 1.
Counter >> total            ^ tally.
Counter >> bumpFromInstance tally := tally + 1.

Counter initTally.
Counter bump.
Counter new bumpFromInstance.
Counter new total.          "=> 2"
```

A class variable is read and assigned from instance-side and class-side
methods alike, and the store always reaches the one shared slot on the class
that declares it.

**Class-instance variables** — one slot per class, not shared with
subclasses — are declared on the class side and used from class-side
methods:

```smalltalk
Object subclass: #Shape.
Shape class instanceVariableNames: 'count'.
Shape class >> noteOne   count := (count ifNil: [ 0 ]) + 1. ^ count.

Shape subclass: #Circle.
Shape noteOne.
Shape noteOne.        "=> 2"
Circle noteOne.       "=> 1"
```

Class-side methods do not see instance variables.

The optional `uses:` clause declares **multiple inheritance / mixins** — see
§4.11. It takes a collection of class objects (typically a `{ … }` dynamic
array) which become additional parents alongside the primary superclass.

`subclass:` is also an ordinary message understood by every class object, so a
class reached through an *expression* — for instance a class imported from a
module — can be subclassed too: `(lib Counter) subclass: #Fast`. The
`uses:` clause has the same expression-receiver form:
`(lib Counter) subclass: #Fast uses: { LoggingMixin }`.

### 3.3 Method definitions

```
methodDecl ::=  Identifier ('class')? '>>' selectorPattern localVars? statement*
selectorPattern ::=  Identifier                              "unary"
                  |  BinaryOp Identifier                     "binary"
                  |  ( Keyword Identifier )+                 "keyword"
                  |  Identifier '(' paramList ')'            "call-form"
paramList  ::=  (param (',' param)*)?
param      ::=  Identifier                                   "positional"
             |  Identifier '=' callValue                     "named with default"
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

Counter >> incr(by, factor = 1)   "call-form: 1 positional, 1 named-with-default"
  value := value + (by * factor).
  ^ value.
```

A **call-form** method declares positional parameters (no default) and
optional named parameters (each with a default expression). Defaults
evaluate at call time in the method's own scope — they may reference
earlier positional parameters and `self`. Positional parameters must come
first; named parameters are reordered alphabetically by the parser and
the corresponding default expressions stay paired with their key.

A call-form method is registered on the class under the **bare name** as
its attribute key (no colons), matching the protoCore method convention.
This is distinct from a `>> bar:` keyword method of the same root name,
so the two forms coexist freely. A class cannot however host both a
unary `>> bar` and a call `>> bar(...)`: both register under attribute
key `bar` and the second declaration overrides the first.

A method body ends at the first of:

- a **blank line** — whether or not the last statement has a period (D33);
- the start of the next method declaration (`Name >> …`, `Name class >> …`),
  with or without a period before it; a class declaration (`… subclass: …`)
  also ends it when the statement before it ends with a period;
- an **unindented line** (a token at column 1) after the body's first
  statement — method bodies are indented, top-level statements are not;
- the first **top-level `^` statement**: anything after it is read as a new
  top-level form. A `^` nested in a block does not end the body.

A statement may not span a blank line outside parentheses, brackets or
braces, so a method is written without blank lines inside it, and a
top-level statement that follows a method needs a blank line before it. A
method with no `^` returns `self`.

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
unarySend    ::=  primary ( Identifier | callTail )*
callTail     ::=  Identifier '(' callArgs ')'
primary      ::=  literal | Identifier | 'self' | 'super' | 'thisContext'
               |  Identifier '(' callArgs ')'     "bare call-form (implicit self)"
               |  '(' expression ')' | block | arrayLit | dynArrayLit
callArgs     ::=  (callArg (',' callArg)*)?
callArg      ::=  Identifier '=' callValue        "named — name binds value"
               |  callValue                        "positional"
callValue    ::=  unarySend ( BinaryOp unarySend )*   "binary chain, no ',' / '='"
```

#### 3.5.1 Call-form messages — protoCore convention

In addition to the three native Smalltalk forms, protoST accepts a **call-
form** send that mirrors the protoCore method convention (name + positional
vector + named dict). It binds at unary precedence, so it composes cleanly
with binary and keyword sends:

```smalltalk
recv name(p1, p2, k1 = v1, k2 = v2)
```

Inside the parentheses, positional arguments come first, then **named
arguments** of the form `Identifier '=' value`. The argument list is
separated by commas. A bare `name(args)` at primary position desugars to
`self name(args)`.

**Selector identity.** A call-form method is a *distinct attribute* from
a keyword method of the same name. A class may host both `>> bar(x)` and
`>> bar: x` and they are independent. The call-form method is stored on
the class under the **bare name** (no colons), matching the protoCore
convention. A single class cannot host both a unary `>> bar` and a call
`>> bar(...)`: both register under attribute key `bar` and the second
declaration overrides the first.

**Argument evaluation order.** Positional arguments evaluate in source
order. **Named arguments evaluate in alphabetical-key order**, not source
order. Side-effect-sensitive code should use explicit temps:

```smalltalk
| t1 t2 |
t1 := expensive1.
t2 := expensive2.
obj op(t1, alpha = t2)        "predictable evaluation order"
```

**Equality versus named binding.** Inside a call-form argument list, an
identifier immediately followed by `=` at the start of an argument is
the named-binding marker. To pass an equality test *as a positional
value*, parenthesise: `f((a = b))`.

```smalltalk
counter incr(2, factor = 3)              "positional + named override"
counter incr(1)                          "named defaults apply"
```

The call form is shaped for methods exported by other protoCore runtimes, but
a call-form send to a *foreign* method is not implemented yet: it reaches only
protoST methods and primitives ([`INTEROP.md`](INTEROP.md) §3.5).

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
    Integer
      SmallInteger
      LargeInteger
    Float
    Fraction
  Character
  Boolean               (the class of true and false)
  String
    Symbol
  BlockClosure          (also reachable as Block)
  UndefinedObject       (the class of nil)
  Actor
  Future
  Atom
  Exception             (see §8.1 for its subclasses)
  Collection
    SequenceableCollection
      Array
      OrderedCollection
        SortedCollection
      Interval
    HashedCollection
      Set
      Bag
      Dictionary
  Association
  Point, ReadStream, WriteStream, Message, Date, Time, Duration, Random
```

There are no `True`, `False` or `Magnitude` classes: `true class` and
`false class` answer `Boolean`, and `Character` and `Number` descend directly
from `Object`.

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

A new instance is created by sending `new` to the class:

```smalltalk
a := Account new.
```

`new` returns a fresh, mutable child of the class prototype, with its
instance variables at `nil`, and sends it `initialize` before answering it,
as in Pharo: `Behavior>>new` is `self basicNew initialize`. `basicNew`
allocates without sending `initialize`. A class that defines no `initialize`
inherits an empty one. Built-in collection classes send `initialize` to
instances of user subclasses too (`OrderedCollection subclass: #Stack`, then
`Stack new` is a `Stack`).

`newChild` is the underlying prototype operation: it answers a fresh child of
the receiver and, like `basicNew`, does not send `initialize`.

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

`super` is only meaningful inside a method body. The defining class is
resolved by **object identity**, not by re-resolving its name through the
global namespace: a `super`-send walks the receiver's actual prototype chain
to find the class object that owns the running method, then continues at that
class's superclass. This makes `super` correct even when the defining class is
not a top-level global — notably a class defined inside an imported module
(see [§4.10]).

When the defining class has **several parents** (a class assembled with
`uses:` mixins — see §4.11), `super` searches them in the documented
resolution order: the primary superclass subtree first, then each mixin
subtree in listed order. It binds to the first match. With a single parent
this is identical to the single-inheritance behaviour above.

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

Class-side methods do not see instance variables. They read and assign class
variables and class-instance variables (§3.2).

### 4.8 `printString`

Every object responds to `printString`, which returns a human-readable
`String`. As in Pharo, `printString` is built on `printOn: aStream`, and the
default `Object>>printOn:` writes:

- for a class object, the bare class name (`Counter`);
- for an instance, `a ClassName`, or `an ClassName` when the class name
  starts with a vowel (`a Counter`, `an Account`).

Built-in values print as Smalltalk literals: the `printString` of the string
`abc` is the five characters `'abc'`, quotes included; `#(1 $a 'b')` prints as
`#(1 $a 'b')`, `3 / 4` as `(3/4)`, and other collections with their elements
(`an OrderedCollection(1 2)`).

A class customises its printed form by overriding `printOn:` (preferred) or
`printString`; either one is used by `printNl`, `displayNl`, collection
printing and the CLI. `printNl` writes `printString` followed by a newline and
answers the receiver. `displayString` / `displayNl` are the same except that
a String or Symbol is shown without quotes.

### 4.9 Globals

The global namespace holds all class names and any name assigned at module top
level. A free identifier in an expression that is not a local, argument, or
instance variable resolves as a global. A failed global lookup is a runtime
error (`undefined global: X`).

### 4.10 Extensible classes from modules

`subclass:` is not only the textual class-declaration form ([§3.2]) — it is
also a **runtime message** understood by every class object. This lets a
program import a class from a module, subclass it locally, override its
methods, and call `super` in an override to reuse the module's original
implementation:

```smalltalk
json := Import from: 'json'.

"Subclass an imported class. The receiver is an expression (the module
 attribute `json JSON`), so the message form of `subclass:` applies."
json JSON subclass: #TaggedJSON instanceVariableNames: ''.

"Override a class-side method; `super` reaches the imported JSON's."
TaggedJSON class >> stringify: anObject
  ^ 'json:' , (super stringify: anObject).

TaggedJSON stringify: #(1 2).          "=> 'json:[1,2]'"
(TaggedJSON parse: '[1, 2]') size.     "=> 2 — parse: is inherited unchanged"
```

`examples/modules/02_subclass_imported.st` does the same with instance-side
methods: it subclasses the `Rectangle` of `examples/modules/_geometry.st` and
calls `super area`. A module name is resolved against the current directory
(then `$STPATH`, the active venv and `lib/`), not against the directory of the
importing file, so run that example from `examples/modules/`.

How it works:

- **`subclass:` / `subclass:instanceVariableNames:`** sent to any class object
  return a fresh prototype child of that class, stamped with the new class
  name and bound as a global under that name (so a following
  `NewClass >> selector` method definition resolves it). The new class's
  parent is the receiver — *the actual class object*, even when it lives in
  another module — so method lookup, instance-variable access, and `super`
  all traverse the real prototype chain by identity.
- **Overriding** is ordinary: a `NewClass >> selector` method shadows the
  inherited one for instances of `NewClass`.
- **`super`** in an override resolves the defining class by walking the
  receiver's prototype chain (see [§4.6]); the next class searched is that
  class's superclass — the imported module class — so the module's
  implementation runs. This holds across any depth of chain (a subclass of a
  subclass of a module class) and regardless of module boundaries.
- Instances of the subclass are instances of the subclass (their
  `printString` reports the subclass name) and still inherit every other
  method of the imported class.

> The textual form `Identifier subclass: #Name …` ([§3.2]) is unchanged; it is
> simply the special case where the superclass is named by a bare identifier.
> When the superclass is reached through an expression — a module attribute, a
> block result, an array element — the message form above applies.

### 4.11 Multiple inheritance and mixins (`uses:`)

A class may be defined with **several superclasses / mixins**. A *mixin* is not
a separate kind of entity — it is just a class. "Mixing in" means adding that
class as an additional parent. The `uses:` clause of a class declaration takes
a collection of class objects (typically a `{ … }` dynamic array) to add as
extra parents, alongside the primary superclass:

```smalltalk
Object subclass: #Comparable.
Comparable >> > other  ^ (self compareTo: other) > 0.
Comparable >> < other  ^ (self compareTo: other) < 0.

Object subclass: #Printable.
Printable >> describe  ^ 'a ', self typeName.

Object subclass: #Money
  instanceVariableNames: 'cents'
  uses: { Comparable. Printable }.

Money >> compareTo: other  ^ cents - other cents.
Money >> typeName         ^ 'Money'.
Money >> cents            ^ cents.
Money >> cents: n         cents := n.

"A Money now understands >, < (from Comparable) and describe (from Printable)."
a := Money new cents: 500.
b := Money new cents: 250.
a > b.                    "=> true"
a describe.               "=> 'a Money'"
```

`uses:` is also available in the message form, so the primary superclass may
be an expression — including a class imported from a module:
`(lib Counter) subclass: #Fast uses: { LoggingMixin }`. The two forms
`subclass:uses:` and `subclass:instanceVariableNames:uses:` are ordinary
messages on every class object.

**Resolution order.** Method and attribute lookup walks the parents
**depth-first, left-to-right**: the primary superclass subtree first, then each
`uses:` mixin subtree in the order listed. A selector reachable through two
parents (the *diamond* case) resolves to the **first** in this order; shared
ancestors are visited once. `super` from a method of a multiply-inheriting
class searches this same order, starting after the method's defining class.

**Instance variables.** Each parent may declare its own instance variables;
they combine as the union. A mixin declaring `instanceVariableNames:` works as
a parent: the mixin's own methods read and write its instance variables on an
instance of the using class (instance variables are resolved by name on
`self`, walking the prototype chain), and a method written on the using class
may name them too: they are the same slots.

> A class assembled with `uses:` has its full parent chain baked in at
> definition time, before any instance exists. Composing a *further*
> behaviour into a class *after* it is defined is the on-the-fly capability
> of §4.12.

### 4.12 On-the-fly behaviour composition (`addBehavior:`)

A behaviour can be composed into a class **at runtime**, with no
recompilation. A *behaviour* is just a mixin — an ordinary class carrying
methods. `addBehavior:` adds it as a further parent of the class:

```smalltalk
Object subclass: #Greeter.
Object subclass: #Loud.
Loud >> shout  ^ 'HEY!'.

Greeter addBehavior: Loud.
Greeter new shout.          "=> 'HEY!' — Greeter gained Loud's behaviour at runtime"
```

After `addBehavior:`, the class object **and every instance created from it
afterwards** respond to the mixin's methods. The class keeps all of its own
methods, its instance variables, and its `super` path; the mixin is searched
**after** the class's existing superclass / `uses:` subtrees, consistent with
the [§4.11] resolution order. `addBehavior:` composes freely with `uses:` — a
class defined with mixins can be given still more behaviour later — and may be
called more than once.

`addParent:` is a lower-level alias of `addBehavior:` — the same operation
named after the underlying prototype mechanism.

This is the most direct demonstration of what the prototype kernel allows: a
class's behaviour assembled incrementally at runtime from independent mixins.

> **Future instances only.** `addBehavior:` affects the class object and every
> instance created *after* the call. An instance created *before* the call
> does **not** gain the new behaviour — it keeps the parent chain it was
> constructed with. This is a deliberate, documented limitation
> (`STATUS.md` D21): protoCore captures an object's parent chain at
> construction and the object never re-reads it, so a class can only present a
> new chain to *future* instances. (Methods installed directly on a class with
> `>>` *are* seen by pre-existing instances — only new *parents* are not.)
> Lifting this to "all instances" would require protoCore to make a
> constructed object observe later parent mutations of its prototype, which it
> deliberately does not do.

`removeBehavior:` is **not provided**: protoCore's parent API offers no clean
removal of a parent baked into a frozen base chain, so it is out of scope for
this release.

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

The runtime sends `doesNotUnderstand: aMessage` to the receiver, where
`aMessage` is a `Message` answering `selector` and `arguments`. A class may
override it to intercept unknown sends (a proxy, a forwarder); whatever the
override answers is the value of the original send:

```smalltalk
Object subclass: #Echo.
Echo >> doesNotUnderstand: aMessage
  ^ aMessage selector.

Echo new frobnicate: 1 with: 2.    "=> #frobnicate:with:"
```

The inherited `Object>>doesNotUnderstand:` signals a
**`MessageNotUnderstood`** exception — a subclass of `Error` — through the
normal exception machinery. An `on: Error do:` (or
`on: MessageNotUnderstood do:`) handler catches it, and the caught exception's
`messageText` reads `doesNotUnderstand: <selector> (receiver class: <class>)`.
With no handler the run aborts (an actor rejects its `Future`; a script
terminates with an error message and a trace).

### 5.3 `super` dispatch

A `super`-send (`super selector ...`) is resolved exactly as in [§5.1] except
that the lookup begins at the **parents of the defining method's class**,
skipping any override on the receiver itself. The receiver bound to `self` in
the invoked method is still the original receiver.

With single inheritance the defining class has one parent and the lookup
begins there. With multiple parents (a class assembled with `uses:` mixins —
see §4.11) the parents are searched in the resolution order: the primary
superclass subtree first, then each mixin subtree in listed order; `super`
binds to the first match.

The defining class is located by walking the receiver's prototype chain by
**object identity** for the class whose name matches the one the compiler
recorded for the running method. Because the chain is traversed by identity —
not by re-resolving the class name through globals — `super` is correct even
when the defining class is not a global, such as a class defined inside an
imported module and then subclassed locally (see [§4.10]).

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

Every evaluation of a block has its own arguments and temporaries. A block
created in each iteration of a loop captures that iteration's variable, and a
block argument or temporary that reuses an enclosing name is a distinct
variable that shadows the outer one:

```smalltalk
blocks := OrderedCollection new.
1 to: 3 do: [ :i | blocks add: [ i ] ].
(blocks collect: [ :b | b value ]) asArray.    "=> #(1 2 3)"
x := 10.
[ :x | x * 2 ] value: 3.                       "=> 6"
x.                                             "=> 10"
```

### 6.4 Control flow with blocks

protoST has no built-in control-flow statements. Conditionals and loops are
**ordinary message sends** that take blocks as arguments.

**Conditionals** — sent to a boolean:

```smalltalk
x := 5.
(x > 0) ifTrue: [ 'positive' ].                 "=> 'positive'"
(x > 0) ifFalse: [ 'non-positive' ].            "=> nil"
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
sum := 0.
1 to: 5 do: [ :i | sum := sum + i ].          "i takes 1,2,3,4,5"
sum.                                          "=> 15"
10 to: 1 by: -1 do: [ :i | i printNl ].       "counts down"
```

**Yieldable collection iteration** — `doYielding:` is the compiler-
recognised counterpart of `do:` for `SequenceableCollection`s
(Array, OrderedCollection, Interval, String) when the block needs to
cooperatively `wait` inside an actor method:

```smalltalk
sensors doYielding: [ :s | results add: (s read) wait ].
```

`do:` itself is a polymorphic primitive that loops in C++ and calls
the block via a recursive engine; a cooperative yield inside the
block loses the iteration state. `doYielding:` instead emits a
bytecode loop using `at:` + `value:` that lives entirely inside the
engine's dispatch, so the block may yield at any iteration without
losing place. A receiver that does not respond to `at:` (a non-empty
`Set`, `Dictionary` or `Bag`) raises `doesNotUnderstand: at:` at runtime —
iterate those with `do:`, which may not contain `wait`, or convert them
first (`aSet asArray doYielding: […]`). See
Chapter 10.8 of [the tutorial](TUTORIAL.md) for a worked example.

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
Exception                  (root; resumable)
  Error                    (NOT resumable)
    ArithmeticError
      ZeroDivide           (resumable)
    MessageNotUnderstood
    SubscriptOutOfBounds   (a bad index: #(1 2) at: 5)
    KeyNotFound            (Dictionary>>at: with an absent key)
    NotFound               (remove: of an absent element)
    BlockCannotReturn      (§7.1)
    ModificationForbidden  (at:put: on a String)
  Warning                  (resumable)
```

- **`Exception`** — the root. Resumable by default.
- **`Error`** — a serious fault. **Not resumable** — calling `resume:` on an
  `Error` is itself an error. `ZeroDivide` is the exception: as in Pharo it
  is resumable, so `[ (1 / 0) + 1 ] on: ZeroDivide do: [ :e | e resume: 5 ]`
  answers `6`.
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

- `Error` — aborts the current activation (an actor rejects its `Future`; a
  script terminates with exit status 1). An uncaught error prints `error: `
  and its description — `AppError: too much` for a subclass of `Error`, the
  bare text for a plain `Error` or a `doesNotUnderstand:` — followed by one
  `at Class>>selector (file:line)` line per active method, innermost first
  (kernel methods included, with the path of their `lib/kernel/` file):

  ```
  error: AppError: too much
    at Acct>>check: (bank.st:6)
    at Acct>>withdraw: (bank.st:4)
    at <module> (bank.st:9)
  ```
- `Warning` — prints `Warning: <text>` and resumes with `nil`.
- `Exception` — the same as `Warning`: it prints its text and resumes with
  `nil`.

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

Resumability is a class-derived property, answered by `isResumable`:
`Exception`, `Warning` and `ZeroDivide` are resumable, `Error` and its other
subclasses are not. A `resume:` on a non-resumable exception is itself an
error. An exception translated from a native C++ exception (see [§8.7]) is
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
      SortedCollection        kept in order by a sort block
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
`#(10 20 30) at: 2` evaluates to `20`. An out-of-range index signals a
`SubscriptOutOfBounds` error catchable by `on:do:`.

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
sum := 0.
#(1 2 3) do: [ :e | sum := sum + e ].
sum                                              "=> 6"
(#(1 2 3 4) collect: [ :e | e * e ])             "=> #(1 4 9 16)"
(#(1 2 3 4) select: [ :e | e isEven ])           "=> #(2 4)"
(#(1 2 3 4) inject: 0 into: [ :a :e | a + e ])   "=> 10"
(#(3 1 4 1) detect: [ :e | e > 2 ])              "=> 3"
(#(1 2) , #(3 4))                                "=> #(1 2 3 4)"
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
| `remove:` | remove a matching element; signals `NotFound` if absent |
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

> **Which equality decides membership.** `=`, with `==` as a fast path. "Already
> present" in a `Set`, "the same key" in a `Dictionary` and "the same element"
> in a `Bag` all mean the same thing, and they mean what `=` means everywhere
> else in the language. Two consequences are worth stating because they are the
> ones people trip over:
>
> - **Numbers compare across the tower.** §12.2 says `2 = 2.0`, so a `Set`
>   holding `1` includes `1.0`, adding both leaves one element, and
>   `aDictionary at: 1 put: x` is read back by `at: 1.0`. Storing under an equal
>   key of another kind replaces the value and keeps the key already stored.
> - **A NaN is found only by identity.** §12.2 also says `Float nan = Float nan`
>   is false, so a NaN can never be found by value. The `==` fast path means the
>   very NaN object a collection holds is still found; a *different* NaN object
>   is a different element. `Set new add: Float nan; add: Float nan; yourself`
>   therefore has two elements.
>
> A class that overrides `=` must override `hash` to match, as in any Smalltalk:
> the collections group elements by `hash` and only then compare with `=`, so two
> objects that are `=` but hash differently will both be stored.

> **Iteration order is unspecified.** `do:`, `keysDo:`, `valuesDo:`,
> `keysAndValuesDo:`, `associationsDo:`, `keys`, `values`, `associations`,
> `collect:`, `select:`, `detect:` and `asArray` visit a hashed collection in an
> order that depends on the elements' hashes and is not the insertion order.
> It is stable within one run for one collection, and nothing more is promised —
> in particular it may differ between runs and between releases. `detect:`
> answering "the first element" (§9.2) is therefore only meaningful on a
> sequenceable receiver. Use an `OrderedCollection` when the order matters.
> A `Bag` is the exception: it stores one slot per occurrence and `do:` visits
> them in the order they were added.

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

Element equality is the rule stated in §9.6. Unlike `Set` and `Dictionary`, a
`Bag` keeps its occurrences in the order they were added, and `do:` visits them
in that order.

### 9.8 `Dictionary`

A map from arbitrary object keys to values.

| Operation | Meaning |
|-----------|---------|
| `Dictionary new` | a new empty dictionary |
| `at:put:` | store a key/value (returns the value) |
| `at:` | the value for a key; an absent key signals `KeyNotFound` |
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

Keys may be symbols, strings, integers, or other objects. Two keys are the same
key when they are `=`, by the rule in §9.6 — so a key `1` is found as `1.0` —
and the entries are visited in the unspecified order §9.6 describes. A key whose
value is `nil` is still a key: `includesKey:` answers `true` for it and `at:`
answers `nil` rather than signalling.

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

A `String` is not wrapped this way: strings are immutable values (D34), and
`aString at: 1 put: $x` signals `ModificationForbidden`. Build strings with
`,`, `WriteStream on: String new`, `String new: n withAll: $c` or
`copyReplaceAll:with:`.

---

## 10. Actors and futures

protoST's concurrency model is the actor model. It is built in, not a library.

### 10.1 Promoting an object to an actor

`anObject asActor` returns an **actor proxy** wrapping `anObject`. The wrapped
object is unchanged.

```smalltalk
sensor := TempSensor new.
actor := sensor asActor.
```

There are **three priority bands**:

```smalltalk
control  := dispatcher asHighPriorityActor.   "drained first"
data     := sensor      asActor.              "default; medium"
telem    := log         asLowPriorityActor.   "drained last"
```

The scheduler keeps three lock-free ready queues, drained in strict order
(High → Medium → Low). Within a band, the order in which ready actors are
picked is unspecified; *within* one actor, mailbox order is preserved
exactly as before. Priority affects WHICH actor a worker picks up next,
never the within-actor sequence — the single-method invariant
(§10.4) is unchanged. Use `asHighPriorityActor` for control-plane
messages that must drain ahead of bulk data; `asLowPriorityActor` for
background hygiene (telemetry flush, GC hints, log rotation) that should
yield to anything else.

Internally the priority is the actor's `__priority__` attribute
(SmallInteger 0/1/2). Absence is read as Medium, so any code written
before priority bands existed keeps the same scheduling behaviour.

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
| `wait` | block until settled; return the value, or re-signal the rejection (see §10.7) |
| `thenDo:` | register a block to run with the value when resolved |
| `catch:` | register a block to run with the cause when rejected |
| `resolve:` | resolve the future with a value (settles waiters/callbacks) |
| `rejectWith:` | reject the future with a cause |
| `Future whenAll: futures` / `f1 & f2` | a future of the Array of all values; rejected if any is rejected |
| `Future whenAny: futures` / `f1 \| f2` | a future of the first value to arrive |

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
> thread** until the future settles — intentional, the main thread acts as a
> synchronous client of the actor world.

### 10.5 `self` inside an actor

Inside a method running on behalf of an actor, `self` is the **wrapped base
object**, not the actor proxy. A self-send is therefore an ordinary
synchronous dispatch — it does *not* re-enqueue on the actor. The actor
boundary is crossed only by sending to the proxy.

### 10.6 The synchronization boundary

The serialisation belongs to the **actor proxy** and its mailbox, not to the
wrapped object. The wrapped object has no implicit lock; its instance
variables are plain storage.
Three consequences the programmer must honour:

1. **One actor per wrapped object.** Wrapping the same object in two proxies
   and driving both in parallel re-introduces unsynchronised access.
2. **A reference to the object that pre-dates `asActor` bypasses the mailbox.**
   Sending directly to the underlying object after promoting it runs on the
   caller's thread, unsynchronised against the actor's worker.
3. **An actor never reaches inside another actor's wrapped object directly** —
   cross-actor communication is exclusively by message send to the proxy.

### 10.7 Errors in an actor

An exception unhandled inside an actor method propagates to the worker loop,
which **rejects that message's `Future`** with the exception. The actor stays
alive and processes its next message. Partial mutations performed before the
raise are *not* rolled back — protoST has no transactional default.

`wait` on a future rejected with an exception re-signals that exception in the
waiter, with its class and text: an `AppError` signalled inside the actor is
caught by `on: AppError do:` around the `wait`, and a `doesNotUnderstand:` by
`on: MessageNotUnderstood do:`. A future rejected with a value that is not an
exception (`aFuture rejectWith: 'cause'`) is re-signalled as an `Error` whose
text is `Future rejected: cause`.

**An actor that waits handles no other message** (D37). While its method is
parked on `wait`, the actor's mailbox is not processed, so two actors that
`wait` on each other could never proceed. The `wait` that would close such a
cycle signals an `Error` whose text starts with `deadlock:` instead of hanging.
Inside actors, chain with `thenDo:` / `catch:` or `Future whenAll:` rather than
waiting on an actor that may call back.

### 10.8 Atoms — optimistic-concurrency cells

An **`Atom`** is a shared mutable cell updated lock-free, by an optimistic
compare-and-swap. It is the third concurrency tool, alongside immutable values
and actors:

- an **immutable value** is shared freely — it cannot change;
- an **actor** serialises arbitrary logic over a piece of state;
- an **`Atom`** is a bare cell that many threads update *directly*, with a
  compare-and-swap retry rather than a lock.

This is the *agent / atom* pair: where an actor is the *agent*, an `Atom` is
the *atom*. Reach for an `Atom` when many actors must update one shared value
— a counter, a registry, a world graph — and routing every update through a
single owner actor would be a bottleneck.

<!-- snippet-group: atom -->
```smalltalk
total := Atom on: 0.        "a cell holding an initial value"
total value.               "=> 0   — read the current snapshot"
```

**`value:ifCurrent:`** is the raw compare-and-swap. It installs the new value
only if the cell still holds the expected one (by pointer identity), and
answers whether it did:

<!-- snippet-group: atom -->
```smalltalk
total value: 1 ifCurrent: 0.   "=> true  — 0 was current, cell is now 1"
total value: 9 ifCurrent: 0.   "=> false — 0 is not current; nothing written"
```

A failed CAS means another thread won the race. The caller drives its own
retry — re-reading the now-current value (the *updated* graph) and rebuilding:

```smalltalk
[ old := total value.
  new := old + 1.
  total value: new ifCurrent: old ] whileFalse.
```

**`swap:`** is that loop, done for you: it applies a block to the current value
and CASes the result in, retrying from a fresh read on contention. It answers
the value finally installed. The block may run more than once, so it must be
side-effect free:

```smalltalk
total swap: [ :running | running + 1 ].
```

Because the comparison is pointer identity over **immutable snapshots**, the
classic ABA hazard does not arise: if the pointer is unchanged the value
genuinely is the one observed. A CAS-retry over an `Atom` therefore needs none
of the guards a compare-and-swap over raw mutable memory would.

**`setInstVar:from:to:`** exposes the same compare-and-swap directly on any
object's instance variable — the unwrapped form, for a CAS on an object you do
not want to wrap in an `Atom`:

```smalltalk
"set #count to new only if it currently holds old; answer whether it did"
account setInstVar: #count from: old to: new.
```

An unset instance variable reads as `nil`; pass `nil` as the expected value to
match it. The name is matched against the receiver's own instance variables,
never its methods.

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
m := Import from: 'json'.
(m JSON parse: '[1, 2, 3]') size.     "=> 3"
```

A module name is looked up, in order, in the current directory, the
directories listed in `$STPATH` (colon-separated), the active venv, and the
standard library `lib/`; `.st` is appended when missing. The directory of the
importing file is not searched.

The classes a module declares are also bound as globals of the running
program, as a class declaration always is (§4.3). Importing `random` or `time`
from `lib/` therefore rebinds the globals `Random`, `Time` and `Duration` to
that module's classes, which have a different protocol from the kernel classes
of the same names (see the tutorial, chapter 9).

Module resolution goes through protoCore's **Unified Module Discovery (UMD)**.
A protoST module provider resolves `.st` files. UMD is shared by the protoCore
runtimes, and protoST's resolution chain can be extended with another
runtime's provider by a program that embeds it; what that has and has not
been shown to do with a real second runtime is recorded in
[`INTEROP.md`](INTEROP.md) §0 (the `protost` binary itself loads only `.st`
modules). Imported modules are cached — importing the same path twice yields
the same module object.

`Import from:` works on every thread: at a script's top level and inside actor
methods alike. A module's top level runs at most once per runtime. When several
actors import a module that is still loading, the later importers wait for the
first load to finish and receive the same module object. A load that fails is
not cached, so every importer sees the error. An import cycle — a module whose
top level, directly or through other modules, imports itself — raises an
`Error` (`cyclic module import: <path>`) instead of waiting forever.

An exception signalled by a module's top level is signalled inside the
importer's `Import from:` send, exactly as if the importer had signalled it
there. A matching handler around the import runs once, with the exception the
module signalled, and every handler action has its usual effect: falling off
the end or `return:` abandons the module's top level and answers from the
importer's `on:do:`, `retry` re-evaluates the protected block (importing the
module again), `resume:` continues the module's top level after a resumable
signal, and `pass` moves the search to the importer's outer handlers. The
module's `ensure:` and `ifCurtailed:` blocks run as the unwind leaves it; an
import nested in another module's top level propagates through both. A module
whose top level does not complete is not cached, so the next import runs it
again. Whatever that top level defined before the exception — classes and
their methods — stays defined.

> The cycle check sees only imports waiting for imports. A module's top level
> must not `wait` on an actor message that itself imports the same module:
> the importer waits for the module, the module waits for the Future, and
> neither finishes.

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
| `new` | a fresh instance, sent `initialize` before it is answered |
| `basicNew` / `newChild` | a fresh instance, without `initialize` |
| `printString`, `printOn:` | human-readable `String` (§4.8) |
| `displayString`, `displayNl` | as `printString` / `printNl`, strings without quotes |
| `printNl` | print the receiver followed by a newline; returns the receiver |
| `class`, `isKindOf:`, `respondsTo:`, `perform:` … `perform:with:with:` | reflection |
| `error:` | signal an `Error` with the given text |
| `doesNotUnderstand:` | sent by the runtime for an unknown selector; overridable (§5.2) |
| `yourself` | the receiver (for cascades) |
| `asActor` | wrap the receiver as an `Actor` (Medium-priority band) |
| `asHighPriorityActor` | same, but the actor sits in the High-priority ready queue (drained before Medium/Low) |
| `asLowPriorityActor` | same, but the actor sits in the Low-priority queue (drained after Medium/High) |
| `->` | build an `Association` (`key -> value`) |
| `==` `~~` | identity / non-identity — same object, or not |
| `=` `~=` | equality / inequality; the default is identity, overridden to value-equality on `SmallInteger`, `String`, `Symbol`, `Boolean` and other value types |
| `isNil` `notNil` | nil test — `false` / `true` for every object except `nil` |
| `ifNil:` `ifNotNil:` `ifNil:ifNotNil:` | nil-conditional evaluation; an `ifNotNil:` block may take the receiver as an argument |
| `on:do:`, `on:do:on:do:`, `ensure:`, `ifCurtailed:` | exception protocol (these are bound on `Block`; see §12.5) |
| `setInstVar:from:to:` | atomic compare-and-swap on an instance variable: set it to the third argument only if it currently holds the second; answer whether it did (see §10.8) |
| `sleep:` | sleep the current thread N ms (a test helper, not for production use) |

### 12.2 `Number`, `SmallInteger`, `LargeInteger`, `Fraction`, `Float`

protoST has a full numeric tower. `SmallInteger` and `LargeInteger` (under
`Integer`), `Fraction` and `Float` all descend from `Number`, and arithmetic, comparison and the unary numeric
operations are bound **once on `Number`**, so every numeric kind understands
the same protocol:

| Selector | Meaning |
|----------|---------|
| `+` `-` `*` | arithmetic |
| `/` | exact division: between integers it answers a `Fraction` in lowest terms (`3 / 4`), or an `Integer` when it divides evenly (`4 / 2` is `2`); `/` by zero signals `ZeroDivide` |
| `//` | integer division, rounded toward negative infinity (`-7 // 2` is `-4`) |
| `\\` | modulo, with the sign of the divisor (`-7 \\ 2` is `1`) |
| `quo:` `rem:` | integer division and remainder truncated toward zero (`-7 quo: 2` is `-3`, `-7 rem: 2` is `-1`) |
| `<` `<=` `>` `>=` | ordered comparison → a boolean |
| `=` `~=` | equality / inequality → a boolean (value equality across the tower, so `2 = 2.0`) |
| `negated` | the receiver with its sign flipped |
| `abs` | the absolute value |
| `isEven` `isOdd` | integer parity (a non-integral `Float` is neither) |
| `printString` | a Smalltalk literal: a `Float` always shows a fractional part (`4.0`, `1.0e16`), a `LargeInteger` shows its exact digits in full, a `Fraction` prints as `(3/4)` |
| `printString:` `printStringRadix:` | the digits in another base (`255 printString: 16` is `'FF'`, `255 printStringRadix: 16` is `'16rFF'`) |

The arithmetic primitives delegate to protoCore's own `ProtoObject`
arithmetic, which gives the tower three properties for free:

- **Mixed-mode coercion.** An operation with one `Float` operand produces a
  `Float`: `1 + 2.5` → `3.5`, `2.5 + 1` → `3.5`, `1 / 2.0` → `0.5`. An
  operation between an `Integer` and a `Fraction` stays exact:
  `(1/3) + (2/3)` → `1`.
- **Transparent overflow promotion.** An integer result that exceeds the
  54-bit inline `SmallInteger` range is automatically promoted to a heap
  arbitrary-precision `LargeInteger` and stays exact — a `whileTrue:` loop
  computing `25!` yields the exact `15511210043330985984000000`, not a
  wrapped value. The boundary is invisible to the program.
- **One protocol.** Because the primitives are bound on `Number`, a `Float`
  and a `LargeInteger` answer exactly the same selectors a `SmallInteger`
  does.

> **Division.** `/` between two integers is exact, as in Smalltalk-80:
> `1 / 3` is the `Fraction` `(1/3)`, `4 / 2` is `2`. If either operand is a
> `Float`, `/` is float division (`1 / 2.0` → `0.5`). A `Fraction` answers
> `numerator`, `denominator` and `asFloat`, and takes part in every
> arithmetic operation and comparison. `ZeroDivide` is a subclass of
> `ArithmeticError` and is resumable.

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
| `reciprocal` | `1 / self` (exact for an integer: `2 reciprocal` is `(1/2)`) |
| `isZero` | comparison with zero → a boolean |
| `min:` `max:` | the smaller / larger of receiver and argument |
| `between:and:` | inclusive range test `low <= self <= high` → a boolean |
| `asFloat` | the receiver as a `Float` |
| `asInteger` | the receiver as an integer (a `Float` is truncated toward zero) |
| `asCharacter` | the `Character` with this code point (the inverse of `Character>>asInteger`) |
| `even` `odd` | integer parity (aliases of `isEven` / `isOdd`) |
| `factorial` | `1 * 2 * ... * n` on a non-negative integer — exact, promotes to `LargeInteger` |
| `raisedTo:` | exponentiation — see below |
| `gcd:` `lcm:` | greatest common divisor / least common multiple of two integers |
| `**` | exponentiation (`2 ** 3` is `8`) |
| `roundTo:` | round to a multiple of the argument |
| `bitAnd:` `bitOr:` `bitXor:` `bitShift:` | bit operations on integers |

Class-side **constants** are bound on `Float`: `Float pi`, `Float e`,
`Float infinity`, `Float nan`.

> **NaN.** Comparisons follow IEEE 754. A NaN is unordered with every number,
> itself included: `<`, `<=`, `>`, `>=` and `=` answer `false` and `~=`
> answers `true`, so `Float nan = Float nan` is `false`, while identity (`==`)
> of one NaN object with itself still holds. `min:` and `max:` answer the
> argument when either side is a NaN (no ordering holds), and `between:and:`
> answers `false`. Collections compare elements and `Dictionary` keys the same
> way, so a NaN element or key is found only by identity. `Float nan` answers
> a new NaN object on every send.

> **Exact exponentiation and factorial.** `raisedTo:` with a non-negative
> integer exponent, and `factorial`, are computed by exact repeated
> multiplication, so each intermediate product promotes to a `LargeInteger`
> the moment it leaves the 54-bit `SmallInteger` range — `2 raisedTo: 100` and
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

### 12.4 `String` / `Symbol` / `Character`

| Selector | Meaning |
|----------|---------|
| `,` | concatenation → a new `String` |
| `size` | character count (Unicode code points) |
| `at:` | the n-th `Character` (1-based); an out-of-bounds index signals `SubscriptOutOfBounds` |
| `at:put:` | signals `ModificationForbidden`: strings are immutable (D34) |
| `asInteger` / `asNumber` | the number the string spells (`'42' asInteger` is `42`, `'3.5' asNumber` is `3.5`), or `nil` when it spells none (`'abc' asInteger` is `nil`) |
| `asSymbol` / `asString` | conversion between `String` and `Symbol` |
| `=` `~=` `<` `>` | content equality and ordering |
| `copyFrom:to:`, `indexOf:`, `occurrencesOf:`, `includesSubstring:`, `beginsWith:`, `substrings:`, `asUppercase`, `asLowercase`, `trimBoth`, `reversed` | the everyday protocol |
| `do:`, `collect:`, `select:`, `first`, `last`, `isEmpty` | the collection protocol, over `Character`s |
| `printNl` / `displayNl` | print followed by a newline, with / without quotes |

A `Character` is written `$a` and answers `asInteger` / `value` (its code
point), `isVowel`, `isLetter`, `isDigit`, `asUppercase`, `asLowercase`;
`Character value: 65` and `65 asCharacter` answer `$A`. `'abc' at: 2` is `$b`.

### 12.5 `Block`

| Selector | Meaning |
|----------|---------|
| `value` … `value:value:value:value:` | evaluate with 0–4 arguments |
| `valueWithArguments:` | evaluate with the elements of an Array as arguments |
| `numArgs` | the number of arguments the block takes |
| `timeToRun` | evaluate and answer the elapsed time as a `Duration` |
| `whileTrue:` | loop: while the receiver block is `true`, evaluate the argument |
| `whileFalse:` | loop: while the receiver block is `false`, evaluate the argument |
| `whileTrue` `whileFalse` | loop: re-evaluate the receiver while it stays `true` / `false` |
| `repeat` | loop forever — exited only by a non-local return |
| `on:do:`, `on:do:on:do:` | run as a protected block |
| `ensure:`, `ifCurtailed:` | run with a cleanup block |

### 12.6 `Future`

`wait`, `thenDo:`, `catch:`, `resolve:`, `rejectWith:`, and the combinators
`Future whenAll:` / `&` and `Future whenAny:` / `|` — see [§10.3](#103-future).

### 12.6a `Atom`

A shared mutable cell with optimistic-concurrency compare-and-swap — see
[§10.8](#108-atoms--optimistic-concurrency-cells).

| Selector | Meaning |
|----------|---------|
| `Atom on: aValue` | a fresh cell holding `aValue` |
| `value` | the current snapshot |
| `value: new ifCurrent: expected` | raw CAS — install `new` iff the cell still holds `expected`; answer whether it did |
| `swap: aBlock` | read-modify-CAS retry loop; applies `aBlock` to the current value, retrying on contention; answers the value installed |

### 12.7 `Exception` / `Error` / `Warning`

| Selector | Meaning |
|----------|---------|
| `signal`, `signal:` | raise the exception |
| `messageText`, `messageText:` | read/set the message text |
| `return:` | handler action — yield a value from `on:do:` |
| `resume`, `resume:` | handler action — resume the protected computation |
| `retry` | handler action — re-run the protected block |
| `retryUsing:` | handler action — replace the protected block with the argument and run it |
| `pass`, `outer` | handler action — continue the handler search outward |
| `description` | the message text, or the class name when there is none |
| `isResumable` | whether `resume:` is allowed (§8.6) |
| `,` | an `ExceptionSet`: `on: ZeroDivide, KeyNotFound do: […]` |

### 12.8 Collections

See [§9](#9-collections) for the full per-class protocol. The shared iteration
protocol (`do:`, `collect:`, `select:`, `reject:`, `detect:`, `detect:ifNone:`,
`inject:into:`, `do:separatedBy:`, `count:`, `anySatisfy:`, `allSatisfy:`, `,`,
`asArray`, `size`, `isEmpty`, `notEmpty`, `species`) is bound on `Collection`.

### 12.9 `Import`

`Import from: aPathString` — load and return a module (see [§11](#11-modules)).

### 12.10 `Transcript`

`Transcript` writes to standard output: `show:`, `showCr:`, `cr`, `tab`,
`space`, `print:` (the argument's `printString`), `display:` and `<<`. Its
output is interleaved in order with `printNl` and `displayNl`.

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
continuation prompt. Each result is printed. The session is persistent:
variables, classes and methods defined at one prompt remain available at the
next. A variable assigned at the prompt is a global ([§4.9](#49-globals)), and a
block evaluated at the prompt reads and assigns it as that global
(`s := 0.` then `#(1 2) do: [ :x | s := s + x ]` leaves `s` at 3), unless the
block declares a temporary or argument of the same name.

Meta-commands begin with `:` and are recognised only at the primary prompt
(never mid multi-line input). An unrecognised `:foo` reports `unknown command`.

| Command | Effect |
|---------|--------|
| `:help`, `:h` | List the meta-commands. |
| `:quit`, `:q` | Exit the REPL (`Ctrl-D` also exits). |
| `:load <path>` | Read a `.st` file and execute it in the current session. Its definitions and variables persist exactly as if the text had been typed. A clean run is confirmed; a parse / compile / runtime error is reported and the session continues. |
| `:reset` | Discard all session state — every user variable, class and method — and start a fresh runtime. |
| `:vars`, `:env` | List the user-defined globals in the session (variables and classes), each with a short rendering of its value. Built-in names (`Object`, `Array`, …) are not shown. |
| `:time <expr>` | Evaluate `<expr>`, print its result, then report the wall-clock time it took. |
| `:history` | Show the most recent input lines. |

Meta-commands are a REPL feature only — they have no effect on
`protost script.st` or `protost -e`.

> Bytecode compilation (`protost compile`) is not implemented and is no longer
> advertised in the usage text. `main:` selector auto-invocation is not
> implemented. See [§14](#14-known-deviations).

### 13.2 Single runtime per process

A protoST runtime must be the **only** `STRuntime` in its process: a second
`STRuntime` in the same process can mis-resolve module imports, because
protoCore's module provider and module cache are process-wide (D2, see
[§14](#14-known-deviations)). The CLI always constructs exactly one, so this
matters only to a program that embeds protoST.

---

## 14. Known deviations

Where protoST differs from Smalltalk-80 (and from Pharo, the dialect most
readers know), on purpose or not yet implemented. The catalogue for a
Smalltalk programmer, with what to write instead, is
[Tutorial chapter 14](tutorial/14-for-the-smalltalk-programmer.md#144-deviations-from-smalltalk-80);
[`docs/STATUS.md`](STATUS.md) is the live tracker with repros, ids and fixing
commits. Summary as of 0.4.0:

- **Deliberate:** programs are files, not an image; a blank line ends a
  method body (D33); a script shows the value of its last statement (D12,
  D12b); strings are immutable (D34); short symbols are represented as the
  equal strings (D35); a few printed forms differ (D36); an actor that waits
  is not re-entrant (D37); recursion depth is bounded with a catchable error
  (D38); one runtime per process (D2); `addBehavior:` reaches future
  instances only (D21); `outer` is an alias of `pass` (D7).
- **Not implemented:** `thisContext` is reserved but inert (D17); the
  metaclass hierarchy is thin (`x class class` works; there is no
  `Metaclass`/`ClassDescription` protocol beyond the reflective messages of
  §14 of the tutorial).
- **Open bugs:** S3 (see `STATUS.md`); S19 was closed in 0.4.0.

Closed before 0.4.0 and now as in Smalltalk-80: `new` sends `initialize`
(D4), `Transcript` (D10), class variables assigned from instance methods (D19),
per-activation block variables (D30), `Character`, exact `Fraction` division,
`doesNotUnderstand:` overrides.

---

*End of the protoST Language Reference.*
