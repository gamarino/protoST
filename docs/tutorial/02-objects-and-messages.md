# Chapter 2 — Everything is an object, everything is a message

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 1](01-introduction.md) · Next: [Chapter 3 — Variables and literals](03-variables-and-literals.md)

---

This is the most important chapter in the tutorial. Everything else is detail.
If you internalise one idea — that protoST has exactly **one** mechanism, the
message send — the rest of the language unfolds from it.

## 2.1 Everything is an object

In protoST, every value is an object. Not "almost everything" — everything.

- The integer `3` is an object.
- The boolean `true` is an object.
- `nil` is an object (the sole instance of `UndefinedObject`).
- A string `'hello'` is an object.
- A block of code `[ :x | x + 1 ]` is an object.
- A *class* — `Object`, `Counter`, `Dictionary` — is an object.
- An exception, a future, an actor: objects.

> **In Python** *almost* everything is an object — but not quite consistently.
> `3` is an object (`(3).bit_length()` works), yet `if` is a keyword, not an
> object, and `+` is special syntax. **In JavaScript** primitives like `3` and
> `true` are *not* objects (they are autoboxed on demand), and `if`/`while` are
> keywords. **In protoST** there are no exceptions and no autoboxing: `3` is a
> genuine object, and so is everything else, all the way down.

## 2.2 Everything is a message send

The only operation in protoST is **sending a message to an object**. A message
send names three things:

1. a **receiver** — the object the message goes to;
2. a **selector** — the name of the message;
3. zero or more **arguments**.

The receiver looks up the selector, finds the matching method, runs it, and the
send produces a value. That value is itself an object, so you can send it
another message. That is the whole computational model.

> **In Python** you call `obj.method(arg)`. **In JavaScript**, `obj.method(arg)`.
> **In protoST**, `obj method: arg`. Same idea: an object, a named operation,
> some arguments. The differences are surface syntax — and one deep one,
> precedence, covered in §2.6.

## 2.3 Three kinds of message

protoST has exactly three message *forms*, distinguished purely by syntax. You
will use all three constantly, so learn to recognise them at a glance.

### Unary messages — no arguments

A unary message is a bare identifier sent to a receiver. No arguments.

```smalltalk
3 factorial      "=> 6   — send #factorial to 3"
42 printString   "=> '42' — send #printString to 42"
'hello' size     "=> 5   — send #size to the string"
-5 abs           "=> 5   — send #abs to -5"
```

```bash
$ ./build/protost -e '3 factorial'
6
$ ./build/protost -e "'hello' size"
5
```

> **In Python** these are no-argument method calls: `(3).factorial()` style,
> or properties: `len('hello')`. Note that protoST has *no* `len()` function —
> there are no free functions at all. `size` is a *message* you send to the
> string. The string knows its own size.

Unary messages **chain left to right**:

```smalltalk
3 factorial printString    "= (3 factorial) printString => '6'"
```

`3 factorial` produces `6`; then `6 printString` produces `'6'`.

### Binary messages — one argument, operator selector

A binary message has an operator-like selector (one or two characters from the
set `+ - * / = ~ < > & | @ ,`) and exactly one argument.

```smalltalk
3 + 4        "=> 7    — send #+ with argument 4 to 3"
10 - 2       "=> 8"
6 * 7        "=> 42"
'a' , 'b'    "=> 'ab' — #, is string concatenation"
3 < 5        "=> true"
3 = 3        "=> true — value equality"
```

```bash
$ ./build/protost -e '3 + 4'
7
$ ./build/protost -e "'a' , 'b'"
ab
```

Here is the first surprise. `+` is **not** an arithmetic operator. It is a
message named `+`, sent to the receiver `3`, with the argument `4`. The integer
`3` has a method whose selector is `#+`. `'a' , 'b'` is the same: the selector
`#,` sent to the string `'a'`, argument `'b'`. There is nothing special about
`+` that `,` does not also have. They are both just binary selectors.

> **In Python/JS** `3 + 4` is operator syntax; under the hood Python dispatches
> to `(3).__add__(4)` but the `+` is grammar. **In protoST** there is no
> "under the hood" — `3 + 4` *is* the message send `+` to `3`. A consequence:
> you can define `+` on your own class exactly the way you define any other
> method (Chapter 5 shows it).

### Keyword messages — named arguments

A keyword message has a selector built from one or more **keyword parts**, each
an identifier ending in a colon, each followed by an argument.

```smalltalk
anArray at: 2                          "selector #at:, one argument"
anArray at: 2 put: 99                  "selector #at:put:, two arguments"
dict at: #key ifAbsent: [ 0 ]          "selector #at:ifAbsent:, two arguments"
1 to: 10 by: 2 do: [ :i | ... ]        "selector #to:by:do:, three arguments"
```

The selector is the concatenation of the parts: `at:`, `at:put:`,
`at:ifAbsent:`, `to:by:do:`. The arguments are *interleaved* with the keyword
parts, which makes a keyword message read almost like a sentence: `array at: 2
put: 99` reads "array, at 2, put 99".

```bash
$ ./build/protost -e '#(10 20 30) at: 2'
20
```

> **In Python** keyword arguments look superficially similar:
> `array.insert(index=2, value=99)`. But in Python the *method name* is
> `insert` and `index`/`value` are named parameters. **In protoST** the keyword
> parts *are* the method name: the selector is literally `at:put:`. There is no
> separate "method name" — `at:` and `at:put:` are two completely different
> selectors, the way `insert` and `replace` are two different method names in
> Python. This is why protoST code has so few abbreviations: a fully spelled-out
> keyword selector *is* the documentation.

A keyword message takes exactly as many arguments as it has keyword parts. You
cannot omit one and you cannot pass extra. There is no `*args`, no default
arguments, no overloading. If you want a variant, you write a differently-named
selector.

## 2.4 Reading message sends

Put the three forms together and you can read any protoST expression. Take:

```smalltalk
account deposit: salary * 12.
```

This is one keyword send. The receiver is `account`, the selector is
`#deposit:`, and the single argument is whatever `salary * 12` evaluates to.
`salary * 12` is itself a binary send: receiver `salary`, selector `#*`,
argument `12`.

And:

```smalltalk
window title size.
```

This is two unary sends: send `#title` to `window` (yielding, say, a string),
then send `#size` to that string.

## 2.5 There are no statements, only expressions

In Python and JavaScript there is a split between *statements* (an `if`, a
`for`, an assignment, a `return`) and *expressions* (`3 + 4`, a function call).
You cannot use a statement where a value is expected: `x = (if a: b else: c)`
is a syntax error in Python.

protoST has almost no such split. A message send is an expression and produces
a value. A conditional is a message send, so it too is an expression and
produces a value. You can assign its result, pass it as an argument, or chain
another message off it. The only true *statements* are the cascade separator,
the period, and the `^` return — and even `^` is just "produce this value and
leave the method".

> **In JavaScript** the ternary `a ? b : c` is the expression form of `if`,
> bolted on precisely because `if` itself is not an expression. **In protoST**
> there is no need for a ternary, because the *ordinary* conditional is already
> an expression. See §2.7.

## 2.6 Precedence: unary, then binary, then keyword

protoST has exactly three precedence levels, and they follow the three message
forms. From tightest-binding to loosest:

1. **Unary** messages bind tightest.
2. **Binary** messages bind next.
3. **Keyword** messages bind loosest.

Within a level, evaluation is strictly **left to right**. And — this is the
part that surprises everyone — **binary operators have no precedence relative
to each other.** `*` does not bind tighter than `+`.

```smalltalk
3 + 4 * 2          "=> 14, not 11"
```

```bash
$ ./build/protost -e '3 + 4 * 2'
14
```

Why `14`? Because both `+` and `*` are binary, both at the same precedence
level, evaluated left to right: `3 + 4` is `7`, then `7 * 2` is `14`. There is
no "multiplication before addition" rule. protoST does not have one.

> **In Python/JS** `3 + 4 * 2` is `11` because `*` has higher precedence than
> `+`. **In protoST** it is `14` because there is no operator precedence at
> all — every binary operator is equal, and it is left to right. This is the
> single most common stumble for newcomers. When in doubt, **parenthesise**:
> `3 + (4 * 2)` is `11` everywhere.

Now combine the levels. Unary binds tighter than binary:

```smalltalk
3 factorial + 4 factorial    "= (3 factorial) + (4 factorial) = 6 + 24 = 30"
```

```bash
$ ./build/protost -e '3 factorial + 4 factorial'
30
```

The unary `factorial` messages are evaluated first (tightest), then the binary
`+`.

And binary binds tighter than keyword:

```smalltalk
anArray at: i + 1 put: x size
```

reads as `anArray at: (i + 1) put: (x size)`. The binary `i + 1` and the unary
`x size` are computed first, then the keyword `at:put:` consumes their results.

Parentheses `( … )` override all of this and contain a complete expression.
When precedence makes an expression hard to read, parenthesise it. Idiomatic
protoST uses parentheses freely.

A worked reading of a dense line:

```smalltalk
total := total + (items at: i) price.
```

- `items at: i` — keyword send, the i-th item.
- `… price` — unary send to that item, its price.
- `total + …` — binary send.
- `total := …` — assignment.

The parentheses are needed because without them `items at: i price` would
parse `i price` as a unary send first (`price` to `i`) and then
`items at: (i price)` — almost certainly not what you meant.

## 2.7 The revelation: there are no control-flow keywords

Here is the idea that most clearly separates Smalltalk from every mainstream
language. **protoST has no `if`, no `else`, no `while`, no `for`, no
`switch`.** They are not keywords. They do not exist in the grammar.

Conditionals and loops are **ordinary message sends**.

A conditional is a message sent to a **boolean**:

```smalltalk
(x > 0) ifTrue: [ 'positive' ] ifFalse: [ 'non-positive' ]
```

Read that as a message send, because that is all it is. `x > 0` produces a
boolean object — either `true` or `false`. You then send *that boolean* the
keyword message `ifTrue:ifFalse:`, with two arguments, both of them **blocks**
(chunks of code in square brackets — Chapter 4). The boolean `true` has a
method for `ifTrue:ifFalse:` that runs the first block; the boolean `false`
has one that runs the second. The conditional is not built into the language —
it is a method on `Boolean`.

```bash
$ ./build/protost -e '(5 > 3) ifTrue: [ 100 ] ifFalse: [ 200 ]'
100
```

Because it is a message send, it is an *expression*: it produces a value (here
`100`), which you can assign or pass on.

A loop is a message sent to a **block**:

```smalltalk
[ i < 10 ] whileTrue: [ i := i + 1 ]
```

`whileTrue:` is a message. The receiver is the block `[ i < 10 ]` — the
condition. The argument is the block `[ i := i + 1 ]` — the body. The `Block`
class has a `whileTrue:` method that evaluates the receiver block, and while it
yields `true`, evaluates the argument block, and repeats.

Numeric iteration is a message sent to a **number**:

```smalltalk
1 to: 10 do: [ :i | ... ]
```

`to:do:` is a keyword message on the integer `1`, with the limit `10` and a
one-argument block. It is `for i in range(1, 11)` — expressed as a message.

> **In Python** `if`, `while`, `for` are keywords baked into the grammar; they
> are statements, not expressions. **In protoST** they are messages — `ifTrue:`
> on a boolean, `whileTrue:` on a block, `to:do:` on a number — and therefore
> they are expressions, definable, overridable, and indistinguishable from
> "user" code. There is genuinely nothing privileged about them. You could
> define your own `unless:` selector on `Boolean` and it would be every bit as
> first-class as `ifTrue:`.

This is why [Chapter 4](04-blocks.md), on blocks, comes so early and matters so
much. Blocks are how you hand code to these control-flow messages. Master
blocks and you have mastered protoST control flow.

## 2.8 Sending a message an object does not understand

If you send an object a selector that nothing in its lookup chain defines, the
send fails with a **`doesNotUnderstand`** condition:

```smalltalk
3 fooBar
```

```bash
$ ./build/protost -e '3 fooBar'
error: doesNotUnderstand: fooBar
```

This is not a crash — it signals a catchable `MessageNotUnderstood` exception,
a subclass of `Error`. You can wrap the send in an exception handler and catch
it ([Chapter 7](07-exceptions.md)):

```bash
$ ./build/protost -e '[ 3 fooBar ] on: Error do: [ :e | e messageText ]'
doesNotUnderstand: fooBar
```

> **In Python** this is `AttributeError: 'int' object has no attribute
> 'fooBar'`. **In JavaScript** calling `(3).fooBar()` is a `TypeError`. In
> protoST it is `MessageNotUnderstood`, and the term of art for it — *does not
> understand* — is itself the Smalltalk way of thinking: the integer does not
> *understand* the message you sent it.

## 2.9 Summary

- Every value is an object. No exceptions.
- The only operation is the message send: receiver, selector, arguments.
- Three forms: **unary** (`3 factorial`), **binary** (`3 + 4`), **keyword**
  (`a at: 1 put: 2`).
- Precedence: unary > binary > keyword; left to right; **no precedence among
  binary operators** (`3 + 4 * 2` is `14`).
- There is no statement/expression split worth speaking of — almost everything
  is an expression with a value.
- There are **no control-flow keywords**. `ifTrue:` is a message on `Boolean`,
  `whileTrue:` a message on `Block`, `to:do:` a message on a number.
- An unknown selector signals a catchable `MessageNotUnderstood`.

---

Next: [Chapter 3 — Variables, assignment, literals](03-variables-and-literals.md)
</content>
