# protoCore-style call syntax for protoST (positional + named args)

**Date.** 2026-06-13.
**Status.** Approved; v1 implementation in this commit.
**Tracks.** Cross-language interop (INTEROP.md) and the language reference
(LANGUAGE.md §3.5, §4).

## 1. Motivation

protoST today exposes only Smalltalk-80 message forms — unary
(`recv sel`), binary (`recv op arg`), keyword (`recv k1: a1 k2: a2`).
The other runtimes in the protoCore ecosystem (protoPython, protoJS,
and protoCore primitives themselves) use the **protoCore method
convention**: a bare attribute name plus a positional argument vector
plus a named-argument dictionary. From protoST today, a Python method
`obj.double_it(x, name="hello")` is unreachable with a non-empty arg
list: there is no keyword form that names `name=` without making it a
keyword-selector colon, and unary chains only fire zero-arg sends.

This document specifies the language-level dual-syntax that closes the
gap. Existing Smalltalk forms keep working unchanged.

## 2. Surface syntax

### 2.1. Call site (consumer)

A new **call-form send** binds at the same precedence as a unary send
— tighter than binary, tighter than keyword:

```smalltalk
recv name(p1, p2, k1 = v1, k2 = v2)
```

Inside the parentheses, a comma-separated argument list. Each argument
is either:

- **Positional** — any expression.
- **Named** — `Identifier '=' expression`. Detected at parse time when
  the argument starts with `Identifier` immediately followed by the `=`
  binary operator. Named arguments must come after every positional.

To pass an equality test *as a positional argument*, the user
parenthesises: `f((a = b))`. Documented as a known constraint.

**Implicit receiver.** At expression-primary position, a bare
`name(args)` desugars to `self name(args)` at the AST level. Inside a
method this is the natural self-send.

### 2.2. Method declaration

A new `>>` form:

```smalltalk
Counter >> increment(by, factor = 1)
    | result |
    result := value + (by * factor).
    value := result.
    ^ result
```

Positional parameters first, then named parameters with `name =
defaultExpr`. Default expressions are evaluated lazily at call time in
the method's own scope, so they can reference earlier parameters and
`self`. Every named parameter has a default.

## 3. Semantics

### 3.1. Selector identity

Call-form methods and keyword-form methods are **distinct attributes**.
A class may have both `>> bar:` (keyword) and `>> bar(x)` (call); they
do not conflict. The call-form method registers on the class under the
**bare attribute key** (just the method name). This matches the
protoCore convention and is what makes interop transparent.

**Known restriction**: a class cannot have BOTH `>> bar` (unary) and
`>> bar(...)` (call) — both register under attribute key `bar`. The
second declaration overrides the first. Tutorial note recommends
choosing one form per name.

### 3.2. Argument evaluation order

Positional arguments evaluate in source order. **Named arguments
evaluate in alphabetical-key order**, not source order. This sidesteps
the need for compiler temps and is explicitly documented. Side-effect-
sensitive callers use explicit temporaries.

### 3.3. Default values

For a method declared `>> bar(a, b, c = 1, d = a + 1)`:

- Positional arity is 2 (`a`, `b`).
- Named arity is 2 (`c`, `d`); both have defaults.
- A call `recv bar(1, 2)` evaluates both defaults at entry.
- A call `recv bar(1, 2, d = 5)` evaluates only the `c` default; `d`
  binds to the passed `5`.
- The default expression `a + 1` reads `a` from the method's locals;
  the order of default evaluation walks the declared-sorted keys, so
  `c` is computed before `d`.

### 3.4. Mismatch handling

- Wrong positional arity → `doesNotUnderstand:` with a descriptive
  message containing both the called and declared selectors.
- A named key not present in the declared named-key set →
  `doesNotUnderstand:` likewise.

## 4. Bytecode encoding

### 4.1. Call descriptor

The whole call-site descriptor is encoded as a **single mangled symbol**
in the const pool:

```
<name> '#' <nPos> [ '#' <namedKey1> [ '#' <namedKey2> ... ] ]
```

`#` is not a legal identifier character. The compiler sorts the named
keys alphabetically before mangling and pushes the named values in
that same sorted order. Examples:

- `print()`            → `print#0`
- `bar(1, 2)`          → `bar#2`
- `bar(1, 2, c=3)`     → `bar#2#c`
- `bar(1, c=3, d=4)`   → `bar#1#c#d`

### 4.2. New opcode

```
SEND_CALL = 39   // arg = const-pool index of the mangled call selector
```

Stack effect: pops `1 + nPos + nNamed` values (receiver, then
positionals in source order, then named values in alphabetical order
of their keys), pushes 1 result.

### 4.3. Unset sentinel

A bootstrap-perpetual singleton (`Bootstrap::unsetMarker`). The
dispatcher writes this value into named slots that the call site
omitted; the method-decl prologue checks each named slot against the
sentinel and evaluates the default expression when matched.

The const pool gains a new `ConstKind::UnsetMarker` resolved at runtime
to `rt.bootstrap().unsetMarker`. The compiler emits the prologue using
existing opcodes:

```
PUSH_LOCAL slot
PUSH_CONST <unset-marker>
SEND_BINARY ==
JUMP_IF_FALSE skip
<default expr>
STORE_LOCAL slot
skip:
```

A dedicated `JUMP_IF_SENTINEL` opcode is a follow-up performance
optimisation; v1 leans on `SEND_BINARY ==` which already does identity
equality through the standard chain.

## 5. Runtime dispatch (`SEND_CALL`)

In the combined SEND handler:

1. Look up the mangled selector const-pool index. Parse it once and
   cache the parsed form in a per-module vector keyed by const index.
2. Pop `nPos + nNamed` values into a small array; pop the receiver.
3. Look up the **bare name** as an attribute on the receiver via
   `getAttribute` chain walk.
4. Dispatch by attribute kind:
   - **Bytecode method**: read declared signature (`nPos_decl`,
     `sortedNamedKeys_decl`). Arity-check; if `callKeys ⊄ declKeys`,
     raise `doesNotUnderstand:`. Build the locals frame, walking
     declared-sorted keys: for each key in `callKeys`, take next value
     from stack; otherwise write the unset sentinel. `pushFrame` and
     dispatch the method body (whose prologue will evaluate defaults
     for sentinel slots).
   - **Primitive method**: forward positionals to the existing
     primitive registry; raise if `nNamed > 0` (no v1 primitive accepts
     named args; reserved for later).
   - **Non-method attribute**: if `nPos == 0 && nNamed == 0`, return
     the attribute (mirrors unary member-access). Otherwise
     `doesNotUnderstand:`.
   - **No attribute**: `doesNotUnderstand:`.

## 6. AST shape

### 6.1. `NodeKind::CallSend`

- `text` = method name.
- `intValue` = `nPos`.
- `intValue2` = `nNamed` (new field on `Node`).
- `boolFlag` = `false` for explicit receiver, `true` for implicit
  (synthetic `Self` receiver).
- `children[0]` = receiver.
- `children[1..1+nPos]` = positional arg expressions, source order.
- `children[1+nPos..1+nPos+nNamed]` = named arg values, in **sorted-key
  order**.
- `stringList[0..nNamed-1]` = named-arg keys, sorted.

### 6.2. `NodeKind::CallMethodDecl`

- `text` = class name.
- `boolFlag` = `classSide`.
- `intValue` = `nPos`.
- `intValue2` = `nNamed`.
- `stringList[0]` = method name.
- `stringList[1..1+nPos]` = positional parameter names, source order.
- `stringList[1+nPos..1+nPos+nNamed]` = named parameter names, sorted.
- `stringList[1+nPos+nNamed..]` = user-declared method locals.
- `children[0..nNamed-1]` = default expressions, sorted-key order.
- `children[nNamed..]` = method body statements.

## 7. Out of scope (v1 non-goals)

- Dedicated `JUMP_IF_SENTINEL` opcode (performance follow-up).
- Built-in primitives accepting named args (signature reserved, no
  implementations yet).
- Call-form `super.bar(...)` super-sends.
- Cascades over call-form sends (`;` after a call-form send is a
  parse error in v1).
- Serialisation of the new `BytecodeModule` signature fields
  (modules are in-memory only today).

## 8. Verification

End-to-end smoke run after build:

```bash
./build_release/protost -e "
Object subclass: #Counter instanceVariableNames: 'value'.
Counter >> init   value := 0.
Counter >> incr(by, factor = 1)   value := value + (by * factor). ^ value.
| c |
c := Counter new.
c init.
(c incr(2, factor = 3)) printNl.   \"expect 6\"
(c incr(1)) printNl.                \"expect 7\"
"
```

Full suite, single-threaded with memory limits:

```bash
systemd-run --user --scope -p MemorySwapMax=0 -p MemoryMax=8G \
  ctest --test-dir build_release --output-on-failure
```

Expected: pre-existing suite unaffected; new unit + conformance
fixtures for the call form pass.
