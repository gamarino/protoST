# Chapter 3 — Variables, assignment, literals

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 2](02-objects-and-messages.md) · Next: [Chapter 4 — Blocks](04-blocks.md)

---

This chapter covers the raw materials of a protoST program: how to name a
value, and how to write literal values directly in source.

## 3.1 Assignment with `:=`

You bind a name to a value with `:=` — colon-equals.

```smalltalk
x := 10.
greeting := 'hello'.
nums := #(1 2 3).
```

> **In Python/JS** assignment is `=` (and JS also has `let`/`const`).
> **In protoST** it is `:=`. The single `=` is *not* assignment — it is the
> binary message for value equality (`3 = 3` is `true`). Keep them straight:
> `:=` stores, `=` compares.

Assignment is itself an **expression** — it produces the value assigned. So you
can chain assignments:

```smalltalk
a := b := 5.    "both a and b become 5"
```

```bash
$ ./build/protost -e 'a := b := 5'
5
```

`b := 5` evaluates to `5`, and that `5` is then assigned to `a`. This is the
same right-to-left chaining as Python's `a = b = 5`.

## 3.2 Where variables live

protoST has four kinds of variable binding. You will meet all of them; here is
the map.

| Kind | Declared by | Scope |
|------|-------------|-------|
| **Global** | A top-level assignment, or a class declaration | The whole module |
| **Method temporary** | `\| name \|` after a method's selector | One method activation |
| **Block temporary** | `\| name \|` inside a block | One block activation |
| **Instance variable** | `instanceVariableNames:` in the class declaration | One object instance |

Class names (`Object`, `Counter`, `Dictionary`) are globals. Any name you
assign at the top level of a script is a global too.

### The top-level temporaries caveat

There is one rule the language reference does not stress, and it is worth
stating loudly because several reference examples gloss over it:

> **`| temps |` declarations are only legal inside a method or a block — never
> at the top level of a script.** At the top level you just assign to a name,
> and it becomes a global.

This *fails* with a parse error:

```smalltalk
"-- NOT valid at the top level of a script --"
| total |
total := 0.
```

This is the correct way to write the same thing at the top level:

```smalltalk
"-- valid: a top-level assignment creates a global --"
total := 0.
total := total + 5.
total.
```

Inside a method or block, `| temps |` is not only legal but required for
locals — Chapters 4 and 5 use it constantly. Just not at the script's top
level.

## 3.3 Comments

A comment is text in **double** quotes:

```smalltalk
"this is a comment"
```

It may span several lines and appear anywhere whitespace may. There is no way
to put a literal `"` inside a comment — the comment ends at the first closing
quote.

> **In Python** comments are `#`. **In JavaScript**, `//` and `/* */`. **In
> protoST** it is `"..."`. And mind the pairing: **double** quotes are
> comments, **single** quotes are strings. They are opposite to what a C-family
> programmer expects.

## 3.4 Numbers

protoST has a numeric tower: `SmallInteger`, `LargeInteger`, and `Float`, all
descending from `Number`.

### Integers

An integer literal is a run of decimal digits. There is no `0x` hex syntax, no
digit separators, no exponent.

```smalltalk
42      0      1000000
```

A negative integer literal is a `-` glued to digits in operand position:

```smalltalk
-5      -100
```

```bash
$ ./build/protost -e '-5 abs'
5
```

Small integers are stored inline (tagged) for speed. When a computation
produces a result too big for the inline range — about 56 bits — it is
**transparently promoted** to a `LargeInteger`, an arbitrary-precision integer.
You never ask for this; it just happens, and the result stays exact:

```bash
$ ./build/protost -e '100 factorial'
93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000
```

`100 factorial` is a 158-digit number, computed exactly. There is no overflow
and no silent wraparound.

> **In Python** integers are arbitrary-precision by default — `100`-factorial
> "just works". **In JavaScript** a plain `Number` is a 64-bit float and
> `100`-factorial loses precision; you need `BigInt` and a `n` suffix. **In
> protoST** it is like Python: integers are exact and unbounded, and the
> `SmallInteger`/`LargeInteger` split is an invisible performance optimisation,
> not something you write.

### Floats

A float literal is digits, a dot, more digits. **Both sides of the dot are
mandatory.** `3.14` is a float; `3.` is the integer `3` followed by a statement
terminator; `.5` is not a float at all. There is no exponent syntax.

```smalltalk
3.14      0.0      100.5      -3.14
```

```bash
$ ./build/protost -e '4.0 printString'
4.0
```

A `Float` always prints with a fractional part — `4.0`, never `4` — so you can
tell a float from an integer at a glance.

### Arithmetic across the tower

Arithmetic, comparison, and the numeric unary operations are defined once, on
`Number`, so every numeric kind speaks the same protocol. Mixed-mode operations
coerce to `Float`:

```smalltalk
1 + 2.5      "=> 3.5  — an integer plus a float is a float"
2.5 + 1      "=> 3.5"
```

```bash
$ ./build/protost -e '1 + 2.5'
3.5
```

A few selectors deserve a note:

| Selector | Meaning | Example |
|----------|---------|---------|
| `+ - *` | arithmetic | `6 * 7` → `42` |
| `/` | division — see below | `10 / 4` → `2` |
| `//` | integer (truncating) division | `7 // 2` → `3` |
| `\\` | modulo (remainder) | `7 \\ 2` → `1` |
| `< <= > >=` | ordered comparison | `3 < 5` → `true` |
| `= ~=` | value equality / inequality | `2 = 2.0` → `true` |
| `negated` `abs` | sign flip / absolute value | `-5 abs` → `5` |

> **The `/` surprise.** Between two integers, `/` is **truncating integer
> division**: `10 / 4` is `2`, `1 / 3` is `0`. protoST has no `Fraction` type,
> so it follows protoCore's integer `/`. Use `//` when you mean integer
> division explicitly. If *either* operand is a `Float`, `/` is float division:
> `1 / 2.0` is `0.5`. **In Python** `/` is always float division (`10 / 4` is
> `2.5`) and `//` is the floor-division operator — so protoST's integer `/`
> behaves like Python's `//`, which catches Python developers off guard.

The math protocol (`sqrt`, `sin`, `ln`, `raisedTo:`, `gcd:`, `min:`/`max:`,
`between:and:`, …) is also on `Number`, always available with no import.
[Chapter 9](09-standard-library.md) covers it.

```bash
$ ./build/protost -e '(5 between: 1 and: 10)'
true
$ ./build/protost -e '(48 gcd: 18)'
6
```

## 3.5 Booleans and `nil`

`true` and `false` are the two booleans — they are literal keywords, and they
are objects (instances of `True` and `False`, subclasses of `Boolean`).

`nil` is the literal keyword for "no object" — the sole instance of
`UndefinedObject`. An instance variable that has never been assigned reads as
`nil`.

```bash
$ ./build/protost -e 'nil isNil'
true
```

> **In Python** the equivalents are `True`, `False`, `None`. **In JavaScript**,
> `true`, `false`, `null` (and `undefined`). protoST has one nil, not two, and
> it is a real object with methods (`isNil`, `ifNil:`, `printString`).

## 3.6 Strings

A string literal is text in **single** quotes. A literal single quote inside is
written by doubling it. There are no backslash escapes.

```smalltalk
'hello'                "the string: hello"
'it''s here'           "the string: it's here"
''                     "the empty string"
```

```bash
$ ./build/protost -e "'it''s here'"
it's here
```

Strings respond to `,` (concatenation), `size` (character count), `at:` (the
n-th character, 1-based), `=`/`~=` (content equality), and more.

```bash
$ ./build/protost -e "'hello' size"
5
$ ./build/protost -e "'abc' , 'def'"
abcdef
```

> **In Python/JS** strings can use either quote style and have backslash
> escapes (`\n`, `\t`). **In protoST** strings are single-quoted only, a
> literal quote is doubled (`''`), and there are no escapes — a newline in a
> string literal is a real newline in the source. Strings are **immutable** —
> like Python `str` and JS strings, every operation returns a new string.

## 3.7 Characters

A character literal is `$` followed by exactly one character.

```smalltalk
$a      $Z      $$      $ 
```

`$$` is the dollar character; `$` followed by a space is the space character.

> **protoST has no separate `Character` class.** This is a deviation from
> standard Smalltalk worth flagging now. A character literal `$a` and indexing
> into a string both produce a **one-character String**, not a distinct
> `Character` object. So `'hello' at: 1` answers the *String* `'h'`. Use
> `Number>>asCharacter` and `String>>asInteger` to convert between a character
> and its code point. Smalltalkers should note this in
> [Chapter 14](14-for-the-smalltalk-programmer.md).

```bash
$ ./build/protost -e "'hello' at: 1"
h
```

## 3.8 Symbols

A symbol is `#` followed by an identifier-like name or an operator. Symbols are
**interned**: every occurrence of the same symbol is the *same* object, so
`#foo == #foo` is true (identity, not just equality).

```smalltalk
#foo            "an identifier symbol"
#at:put:        "a keyword-selector symbol"
#+              "a binary-operator symbol"
```

```bash
$ ./build/protost -e '#foo = #foo'
true
```

Symbols are protoST's lightweight, hashable, unique names. You use them as
dictionary keys, as enumeration values ("an idle pump" → `#idle`), and as the
names of classes and selectors.

> **In JavaScript** there is a `Symbol` type, but JS symbols are *not*
> interned by literal — `Symbol('x') !== Symbol('x')`. protoST symbols *are*
> interned, so they behave more like **Ruby symbols** or **Python's
> interned strings / enum members**: cheap to compare, unique by name. Reach
> for a symbol wherever you would reach for a short, fixed string constant.

A symbol is also a kind of string — `Symbol` is a subclass of `String` — so a
symbol responds to the string protocol too.

## 3.9 Arrays

protoST has two bracketed array literal forms.

### Literal arrays — `#( … )`

`#( … )` is a **literal array**: its elements are compile-time literals only —
integers, floats, strings, characters, symbols, and nested literal arrays.

```smalltalk
#(1 2 3)                 "three integers"
#($a 'str' #sym)         "a character, a string, a symbol"
#(-1 -2 -3)              "three negative integers"
#(1 #(2 3) 4)            "three elements — the middle one is a sub-array"
#()                      "the empty literal array"
```

There is one wrinkle to remember: **a bare identifier inside `#( … )` is read
as a symbol.** So `#(foo bar)` is an array of the two symbols `#foo` and
`#bar` — not a reference to variables named `foo` and `bar`.

```bash
$ ./build/protost -e '#(1 #(2 3) 4) size'
3
$ ./build/protost -e '#(-1 -2 -3) size'
3
```

### Dynamic arrays — `{ … }`

`{ … }` is a **dynamic array**: its elements are arbitrary expressions,
separated by periods, each evaluated at runtime when the literal is reached.

```smalltalk
{ 1 + 1. 2 * 2. 'a' , 'b' }    "evaluated at runtime: 2, 4, 'ab'"
{ }                            "the empty dynamic array"
```

```bash
$ ./build/protost -e "{ 1 + 1. 'x' , 'y' } size"
2
```

Use `#( … )` for fixed, known-at-write-time data; use `{ … }` when the elements
must be computed.

> **In Python** the literal `[1, 1+1, x]` always evaluates its elements — it is
> protoST's `{ … }`. **In JavaScript**, `[1, 1+1, x]` likewise. Neither has an
> exact analogue of `#( … )`, the *all-literal, compile-time* array — the
> closest is a frozen constant. The practical rule: if every element is a plain
> literal, `#( … )` is the idiom; if any element is computed, use `{ … }`.

Both forms produce an `Array`. Chapter 8 covers the full collection family.

## 3.10 Putting it together

A small script using the literals of this chapter (top-level, so no `| … |`):

```smalltalk
"-- inventory.st --"
prices := #(120 80 200 45).
total := prices inject: 0 into: [ :sum :each | sum + each ].
total.
```

```bash
$ ./build/protost inventory.st
445
```

`#(120 80 200 45)` is a literal array; `inject:into:` folds it (Chapter 8);
`total` is a top-level global. Nothing here is a statement keyword — it is all
message sends and assignments.

---

Next: [Chapter 4 — Blocks](04-blocks.md)
</content>
