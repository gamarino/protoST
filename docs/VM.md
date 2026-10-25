# The protoST virtual machine

This document describes the bytecode virtual machine that runs protoST
programs: how source becomes bytecode, how a frame is laid out, every opcode,
and worked examples of the common constructs. It is written for new
contributors. Every bytecode listing in it is real output of
`protost --disasm`, checked by the ctest `docs/vm-examples`; none is written
by hand.

Contents:

- [Start here](#start-here)
- [1. Overview](#1-overview)
- [2. Opcode reference](#2-opcode-reference)
- [3. Worked examples](#3-worked-examples)
- [4. How to add an opcode](#4-how-to-add-an-opcode)
- [5. How to add a primitive](#5-how-to-add-a-primitive)
- [6. Known oddities](#6-known-oddities)

The code described is `main` at commit `e2e930f` (protoST 0.7.0). Function
names are given instead of line numbers, which go stale; grep for them.

## Start here

### Reading order

Read the source in the order a script travels through it:

| Step | Files | Key functions | What happens |
|---|---|---|---|
| 1. Driver | `src/main.cpp` | `main`, `runReporting` | The script is read, parsed and compiled **as one module**, then run. The runtime (`STRuntime`) is built first, so the kernel is loaded before the script is compiled. |
| 2. Lexer and parser | `src/frontend/Lexer.cpp`, `Parser.cpp`, `AST.h` | `Parser::parseModule`, `parseTopForm`, `parseExpression`, `parseBlock`, `parseMethodDecl`, `parseClassDecl`, `parseCallSend` | Text becomes an AST (`ast::Node`). Class declarations (`Object subclass: #C ...`), method declarations (`C >> sel ...`, `C class >> sel ...`) and call-form methods (`C >> name(a, k = 1)`) are top-level forms of the module. Top-level temporaries `\| x y \|` become `x := nil` assignments (`parseTopTemporaries`). |
| 3. Compiler | `src/frontend/Compiler.cpp`, `Compiler.h` | `Compiler::compileModule`, `collectClasses`, `analyseClosures`, `emitStatement`, `emitExpr`, `tryEmitInlinedControl`, `emitCaptureProlog`, `tryEmitInstVarGroup` | Two passes over the AST (class metadata, then closure analysis: which variables blocks capture), then emission. There is no separate desugaring pass: inlining of `ifTrue:`, `whileTrue:`, `to:do:` and `doYielding:` happens during emission. |
| 4. Bytecode | `src/runtime/Opcodes.h`, `BytecodeModule.h`, `BytecodeModule.cpp` | `BytecodeModule::emit`, `emitWide`, `patchArg`, `internSymbol`, `addBlockModule`, `cachedLocalCount`, `cachedExtraOperandStack` | The instruction set and the module: code bytes, constant pool, nested modules (block and method bodies), frame metadata, debug information. |
| 5. VM | `src/runtime/ExecutionEngine.h`, `ExecutionEngine.cpp` | `ExecutionEngine::runWithArgs`, `runLoop`, `pushFrame`, `popFrame`, the `SEND_*` case of `runLoop`, `doesNotUnderstand`, `gcSafepoint` | The dispatch loop. Method and block frames are pushed on the engine's own frame stack; no C++ recursion per Smalltalk call. |
| 6. Runtime | `src/runtime/STRuntime.cpp`, `Bootstrap.cpp` | `STRuntime::construct`, `loadKernel`, `runTopLevel`, `runModuleTopLevel`, `materialize`, `bindPrimitive` | Prototypes, globals, the kernel (`lib/kernel/*.st`, compiled and run like a script), literal materialisation, actors and their worker threads. |
| 7. Primitives | `src/primitives/*.cpp`, `include/protoST/primitives.h` | `install*Primitives`, `PrimitiveRegistry::registerPrim`, `invokeBlock` (`block_prims.cpp`), `prim_Block_on_do` (`exception_prims.cpp`) | Built-in methods written in C++, bound on prototypes under a selector. |

`src/runtime/Disassembler.cpp` prints a module; reading its `listModule`
alongside `Opcodes.h` is a quick way to learn the operand of every opcode.

### Trying things with `--disasm`

```
protost --disasm script.st [args...]
```

compiles the script, prints the listing of its module and of every block and
method body it holds (nested ones included), then runs the script normally;
the program's own output follows the listing on standard output. The kernel
is compiled separately and is not listed. Example 01 in section 3 shows the
listing of `(1 + 2) printNl.`; every example there is such a listing.

How to read a listing:

- `module main: <module>` is the script's top level. A nested module is
  named by its path: `main.3` is block 3 of the top level, `main.3.0` block 0
  of that body. After the colon comes the module's debug name: `<module>`,
  `Counter>>increment` for a method, `[] in Counter>>increment` for a block
  inside a method, `<block>` for a block at the top level. `PUSH_BLOCK 3` in
  `main` builds a closure, or a method wrapper, over `main.3`.
- The second line is the frame layout: `args` (for a method, `self` plus its
  arguments; for a block, its arguments), `locals` (every slot the code
  addresses, arguments included, with their names), and `stack N (deepest
  D)`: D is the deepest the operand stack gets in the module's code, and N,
  the frame's operand-stack capacity, is D floored at 48 (1.3).
- `defining class C` marks a method body, or a block inside one (the class
  `super` sends start above). `call form:` gives a call-form method's
  signature. `slot N is 'k' in words a..b` names the hidden counter of an
  inlined `to:do:` over its loop. `write groups:` lists the entries of the
  instance-variable write groups (`IVAR_GROUP`).
- `constants:` is the constant pool: index, kind, value. Kinds are
  `integer`, `float`, `largeint` (digits and radix), `string`, `symbol` (a
  symbol literal, a selector or a variable name), `char` and `unset` (the
  call-form "argument omitted" marker).
- `code:` has one instruction per line: the word at which it starts, the
  opcode, the operand, and a comment that decodes the operand: the constant,
  a slot's name, a selector and its argument count, a jump's absolute target
  word (`-> 15`). Positions and jumps count 2-byte instruction words. An
  instruction with an `EXTEND` prefix is listed once, with its combined
  operand and a `(wide)` note.

## 1. Overview

### 1.1 Pipeline

```
source text --Parser::parseModule--> AST --Compiler::compileModule--> BytecodeModule
  --STRuntime::runTopLevel--> ExecutionEngine::runWithArgs --runLoop--> value
```

The whole file is one compilation unit: `compileModule` emits every top-level
statement into one module, separated by `POP`, and ends it with `RETURN_TOP`,
so the value of the last statement is the module's value
(`protost --print-last` prints it). The kernel files listed in
`lib/kernel/00-manifest.txt` go through the same path, one module per file
(`STRuntime::loadKernel`), before the script is compiled. Modules are kept
until the runtime is destroyed: method and block objects point into them.

Declarations are statements of the top level. Nothing is defined at compile
time:

- `Object subclass: #C instanceVariableNames: 'a b'` compiles to sends of
  `__subclassNamed:`, `__setClassName:` and `__setInstVarNames:` and a
  `STORE_GLOBAL` (example 04).
- `C >> sel body` compiles the body into a nested module and installs it at
  run time with `PUSH_GLOBAL C`, `PUSH_BLOCK n`, `PUSH_CONST #sel`,
  `SEND_KEYWORD #__installMethod:as:` (`__installClassMethod:as:` for
  `C class >> sel`). A method value is a closure object over its module.

What the compiler needs from declarations it gathers before emitting:
`collectClasses` records every class's instance and class variables, merged
with the classes already compiled in the process (the kernel's, earlier
imports'), so a method knows whether a name is an instance variable.
`analyseClosures` decides which variables blocks capture (1.4).

Two compilation modes differ: in the REPL (`Compiler::setReplMode`) a
top-level assignment binds a global (`STORE_GLOBAL`), so the next input sees
it; in a script it binds a module local slot. Inside a method, a lowercase
name that nothing declares is a compile error.

### 1.2 Instruction encoding and the module

An instruction word is two bytes: the opcode, then an 8-bit operand
(`kInstrSize = 2`). A larger operand is carried by up to two `EXTEND` prefix
words before the real one, each holding the next more significant byte
(`BytecodeModule::emitWide`; Python's `EXTENDED_ARG`). Operands are limited
to 24 bits. The engine and the disassembler decode the prefixes the same
way: `arg = (arg << 8) | next byte` for each word.

- **Positions and jumps** count words. A jump's operand is relative to the
  word **after** the jump: `JUMP`, `JUMP_IF_TRUE` and `JUMP_IF_FALSE` go to
  `next + operand`, `JUMP_BACK` to `next - operand`. The compiler emits a
  forward jump as one word with a placeholder operand; when the target is
  known and the distance does not fit in a byte, it inserts `EXTEND` prefixes
  in front of the jump (`insertExtendPrefixes`, `Compiler::resolveForwardJumps`),
  which does not change the distance from the jump word. A loop widens its
  exit jump before it emits its `JUMP_BACK` (`Compiler::closeLoop`). An
  inlined body may therefore be of any length.
- **Constant pool** (`BytecodeModule::Const`): kinds `Integer`, `Float`,
  `LargeInteger` (decimal or radix digits, rebuilt by every `PUSH_CONST`),
  `String` (a fresh string per `PUSH_CONST`), `Symbol`, `Char` and
  `UnsetMarker`. Symbols are de-duplicated (`internSymbol`); nothing else is,
  so a module may hold the same integer or string twice. Symbol constants
  serve as symbol literals, as selectors and as variable names; the module
  caches each one's interned `ProtoString` on first use (`constSym`,
  `ivSymbol` for the `_iv_<name>` instance-variable key), so a send does not
  re-intern its selector. `nil`, `true`, `false` and blocks are not
  constants: they have opcodes.
- **Nested modules:** every block literal and every method body is a module
  of its own, owned by the module that contains it (`addBlockModule`).
  `PUSH_BLOCK`'s operand is its index.
- **Frame metadata:** `argCount` (for a method, `self` plus its arguments),
  `definingClass`, the call-form signature (`isCallForm`, `callPosArity`,
  `callNamedKeys`), the instance-variable write-group table
  (`InstVarGroupEntry`).
- **Local count** is not stored: `cachedLocalCount` scans the code once for
  the highest slot of a `PUSH_LOCAL` / `STORE_LOCAL` and caches it; the
  engine raises it to at least 4 (`kMinLocals`).
- **Debug information:** one source line per word (`instrLines`), slot names
  (`localNames`), the `to:do:` counter names (`scopedLocals`), a debug name
  and the source file. It is always compiled in; the debugger needs no
  separate compilation mode and there is no line opcode.

### 1.3 Frames

`ExecutionEngine` is non-recursive. A send to a method or a block pushes a
`Frame` on the engine's `frames_` vector and the same `runLoop` continues
with it; `RETURN` pops it and pushes the result on the caller's operand
stack. A `Frame` holds only integers (module, `pc`, slot geometry, frame
ids, flags). Every object a frame holds lives in a slot of the thread's
`ProtoContext::automaticLocals` array, which the garbage collector traces:

```
baseSlot + 0                     captured-variables dict (or nil)
baseSlot + 1                     self
baseSlot + 2 ..                  locals: for a method, slot 0 = self, then the
                                 arguments, then temporaries; for a block, its
                                 arguments, then temporaries
baseSlot + 2 + localCount ..     operand stack, operandStackCapacity slots
```

Frame regions are packed one above the other from a thread-local cursor
(`g_slotCursor`), above the 256 slots reserved for transient pins
(`TransientPin.h`). Engines nested on the C++ stack (1.5) allocate above the
outer engine's frames on the same array. The array grows on demand up to
2^20 slots; a send that would leave fewer than 16,384 free slots signals the
catchable `Error` "stack depth exceeded" (`signalIfTooDeep`).

The operand stack holds `BytecodeModule::operandStackCapacity` slots: the
deepest point of the module's code (`cachedMaxOperandDepth`, one pass over
the code with each instruction's stack effect; a forward jump carries its
depth to its target), floored at 48. So an expression may nest as deeply as
the source writes it. Compiled code cannot overflow it; a hand-built module
that does signals the catchable `Error` `operand stack overflow (N slots)`.

A method frame is its own *home*; a block frame inherits the home of the
method its closure was created in (`homeFrameId`, stamped on the closure by
`PUSH_BLOCK`). `RETURN` (`^`) in a block frame returns from the home (1.7).

### 1.4 Closures, blocks and captured variables

`PUSH_BLOCK n` creates a fresh mutable child of `BlockClosure` with four
attributes:

| Attribute | Value |
|---|---|
| `__bc_ptr__` | the address of the block's `BytecodeModule`, stored as an integer |
| `__captured__` | the creating frame's captured-variables dict, or nil |
| `__home_frame__` | the creating frame's home frame id |
| `__block_self__` | the creating frame's `self` |

A method value is the same object: `C >> sel` installs a closure over the
method body's module under `sel`. A send recognises a method by its
`__bc_ptr__`.

**Captured variables** are not copied. A variable that some inner block uses
(`Compiler::analyseClosures`: declared in a scope and needed by a block
nested in it) is kept in a *captured dict*, a mutable protoCore object whose
attributes are the variables, instead of a frame slot. The dicts of nested
scopes form a chain through their prototype parent, so the scope chain is
the prototype chain:

- a method that declares captured variables opens a fresh root dict
  (`MAKE_CAPTURED 0`) and binds each one in it (`DEFINE_CAPTURED`), an
  argument with its incoming value (copied from its slot), a temporary with
  nil (example 07);
- a block that declares captured variables opens a child of the dict it
  inherited (`MAKE_CAPTURED 1`), so every activation (every iteration of a
  loop, every call of a block factory) has its own bindings;
- `PUSH_CAPTURED` reads through the chain (`getAttribute`), `STORE_CAPTURED`
  writes to the dict that owns the name;
- the top level's captured variables live in a dict the runtime creates for
  the run (`runModuleTopLevel`), so the top level needs no `MAKE_CAPTURED`.

Instance variables are never captured: they are attributes `_iv_<name>` of
`self`, which a block reaches through its `__block_self__`.

**Inlined blocks.** A literal block argument of `ifTrue:`, `ifFalse:`,
`ifTrue:ifFalse:`, `ifFalse:ifTrue:`, `whileTrue:`, `whileFalse:` (zero
arguments, no temporaries) and `to:do:` (one argument, no temporaries, the
argument not captured by an inner block) is not a closure: its body is
compiled into the enclosing code (`tryEmitInlinedControl`, examples 03, 05,
06). `doYielding:` with a literal one-argument block is compiled to an
explicit loop that sends `value:` to the block (example 15). Every other
block is a real closure.

### 1.5 Calling conventions

**Sends.** `SEND_UNARY`, `SEND_BINARY` and `SEND_KEYWORD` expect
`[receiver, arg1 .. argn]` on the operand stack (receiver deepest) and
replace them with the result. The argument count is 0, 1, or the number of
colons in the selector, with no limit (an array of 8 on the C++ stack, a
heap array for more). `SEND_SUPER` is the same with the lookup moved (step
3). The dispatch (the `SEND_*`
case of `runLoop`) tries, in order:

1. **Actor.** A receiver that is an actor proxy, for a selector other than
   the few the proxy answers itself (`isActorLocalSelector`: identity,
   printing, nil tests): the message is queued on the actor's mailbox, the
   actor is scheduled, and a pending `Future` is the result.
2. **Block evaluation.** A receiver that owns `__bc_ptr__` and a selector
   `value`, `value:`, `value:value:`, ... matching the block's argument
   count: a block frame is pushed in this engine (self = `__block_self__`,
   captured dict = `__captured__`, home = `__home_frame__`). This path can
   be suspended by a `Future>>wait` inside the block (cooperative yield).
3. **Lookup.** `methodHolder(receiver)` (protoST's prototype for a primitive
   value, the receiver itself otherwise), then `getAttribute(selector)` up
   the prototype chain. For `SEND_SUPER`, the lookup starts above the
   class that defines the running method instead (found on the receiver's
   chain by name, `definingClass`). A method installed by
   `__installClassMethod:as:` is hidden from a receiver that is not a class.
4. **Not found:** `doesNotUnderstand`: a Smalltalk `doesNotUnderstand:`
   method of the receiver's class runs with a `Message`; otherwise a
   `MessageNotUnderstood` (an `Error`) is signalled, text
   `doesNotUnderstand: sel (receiver class: C)`.
5. **User method** (the attribute owns `__bc_ptr__`): the module's
   `argCount` must be the send's argument count plus one; a frame is pushed
   with local 0 = self = the receiver and the arguments in locals 1..n; then
   the call-entry safepoint (1.6).
6. **A value attribute** (not an integer): a unary send answers the
   attribute itself (member access, used for module exports); any other send
   raises `#sel: the attribute is a value, not a method`.
7. **Primitive** (the attribute is an integer with bit 62 set, the
   primitive's index in the registry): the C++ function is called as
   `fn(rt, ctx, receiver, args, argc)` **in the engine's context**, inside
   `translateNativeException`, which turns a C++ exception it throws into a
   catchable protoST `Error`. `args` points into a C++ array whose values
   are still in the frame's (popped but traced) operand-stack slots.

**Call-form sends** (`SEND_CALL`): `recv name(p1, k = v)`. The operand names
a symbol `name#nPos#key1#key2...` (named keys sorted); the engine parses it
once into a `CallDescriptor` cached on the module. The receiver, the
positional values and the named values (in sorted-key order) are on the
stack. The bare `name` is looked up; a call-form method gets its positional
values in locals 1..nPos and each declared named parameter either the
caller's value or the unset marker, which the method's prologue replaces by
the default expression (example 08). A protoCore method (a module written in
C++ or another language) is called through protoCore's array call form with
the named values as `ProtoKeywordArgument`s; a primitive accepts positional
values only. At most 16 values; actors are refused.

**Blocks evaluated by primitives.** A primitive that takes a block and runs
it (`ifTrue:` when not inlined, `do:`, `inject:into:`, `on:do:`, `ensure:`,
`collect:`, ...) calls `invokeBlock` (`block_prims.cpp`), which runs the
block in a **new `ExecutionEngine` on the C++ stack**, on the same
`ProtoContext`. These nested engines nest strictly LIFO; at most 1,000 can be
live on a thread (`kMaxNestedEngines`), past which the catchable `Error`
"stack depth exceeded (nested block evaluation)" is raised. A `Future>>wait`
inside such a block is not cooperatively yieldable: it blocks its worker
thread.

### 1.6 Safepoints and rooting

protoCore reclaims nothing a context has not handed over: a context's
allocations are chained on its young generation, which is submitted to the
collector only when the context is destroyed or from
`ProtoContext::safepoint()`. protoST runs a whole program on one context per
thread (1.8), so without polling nothing would ever be collected (bug S15 in
docs/STATUS.md). `gcSafepoint` polls `safepoint()` (and the Ctrl-C interrupt
flag) at three points, where every live value is in a traced slot:

- **engine entry** (`runLoop`'s first statement), for loops that a primitive
  drives in C++ (`do:` runs a nested engine per element);
- **call entry**, right after a method or block frame is pushed by a send or
  `SEND_CALL`, so a recursion that never loops still submits;
- **`JUMP_BACK`**, every iteration of an inlined loop.

`PROTOST_NO_GC_SAFEPOINT=1` disables the hook, for measurements only.

Rooting inside the VM (protoCore EMBEDDER-CONFORMANCE rule 3: no
`ProtoObject*` held only in a C++ local across an allocation or a
safepoint): `pop` only decrements `sp`, and the whole slot array is traced,
so a popped operand stays rooted until something overwrites its slot; an
object created in a C++ local and held across another allocation is pinned
with a `TransientPin` (a scratch slot of the same array) until it is pushed.
`PUSH_BLOCK`, `MAKE_ARRAY` and the actor path show the pattern. Interned
symbols are perennial and need no pin.

### 1.7 Exceptions and non-local return

There are **no exception opcodes**. `on:do:`, `on:do:on:do:`, `ensure:` and
`ifCurtailed:` are primitives of `BlockClosure` (`exception_prims.cpp`), and
`signal` / `signal:` primitives of `Exception`:

- `on:do:` pushes an entry (class, handler block, unique id) on the thread's
  handler stack (`HandlerStack.h`), runs the protected block in a nested
  engine and pops the entry on every exit path.
- `signal` walks the handler stack **without unwinding**: the signalling
  frames are still live while the handler block runs (in a nested engine on
  top of them), so `resume:` unwinds only the handler block (`ResumeSignal`)
  and returns from `signal`. `return:`, falling
  off the end of the handler, and `retry` unwind with a C++ exception
  (`UnwindToHandler`, `RetrySignal`) caught by the `on:do:` primitive that
  owns the handler id. An unhandled `Error` reaches the top level as
  `UnhandledSTException` and is reported with the protoST stack.
- `ensure:` runs its block on the way out of normal returns, unwinds and
  non-local returns.
- Errors the engine itself raises (`doesNotUnderstand:`, an undefined
  global, a block called with the wrong argument count, the non-Boolean
  guard, a method's arity, a value attribute sent a keyword message, an
  unresolvable `super`, an operand-stack overflow) are signalled the same
  way and are catchable. Only checks that compiled code cannot fail (a stack
  underflow, a slot out of range, a malformed instruction) throw a plain C++
  exception.

**Non-local return.** `^ expr` compiles to `RETURN`. In a method frame it is
an ordinary return. In a block frame the engine looks for the home method's
frame: if it is in the same engine, every frame above it and the home frame
itself are popped and the value goes to the home's caller; if the home lives
in an outer engine (the block was run by a primitive), a C++
`NonLocalReturn` unwinds the nested engines, running `ensure:` blocks, up to
the engine that holds it; if the home has already returned, the catchable
`BlockCannotReturn` is signalled. `RETURN_TOP` (the end of a block body and
of the top level) always returns locally. Example 18.

### 1.8 Contexts

protoST creates **no `ProtoContext` per method activation and none per
primitive call**. A whole program runs on its thread's root context: the
runtime's root context for the main thread (`STRuntime::rootCtx`), and the
context protoCore gives each worker `ProtoThread` for actor messages. Method
and block frames are regions of that one context's `automaticLocals` (1.3);
primitives receive the engine's `ctx`. The only `ProtoContext` protoST
constructs itself is the root context it makes when it is embedded in
another runtime's `ProtoSpace` (`STRuntime`'s `ownRootCtx`).

## 2. Opcode reference

`Op` (`src/runtime/Opcodes.h`) has **43 values**: 1 to 44 except 7 and 21,
and `EXTEND` (254). The values 0, 7, 21 and 255 are reserved: they were
`NOP`, `PUSH_SUPER`, `DUP_RECEIVER` and `HALT`, removed because the compiler
no longer emitted them (`PUSH_SUPER` since `super` sends became `SEND_SUPER`)
and only hand-built unit tests ran them; a reserved value is an unimplemented
opcode. The dispatch in `ExecutionEngine::runLoop` has a `case` for all 43
and a threaded-dispatch label for the 42 that execute (`EXTEND` is a prefix,
consumed while decoding; a stray one raises `stray EXTEND prefix`). The
compiler emits all of them; `EXTEND` only as a prefix, by `emitWide` and when
it widens a jump.

Notation: the stack effect is `before -> after`, top of stack on the right;
`v` is any value. Operands not listed are emitted as 0 and ignored. "Errors"
lists what the instruction itself raises; a message in `code` font without a
class is a C++ `std::runtime_error`, impossible in compiler output.

### 2.1 Constants and literals

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `PUSH_CONST` | 1 | constant index (wide) | `-> v` | Pushes the constant, materialised on the executing thread's context (`STRuntime::materialize`): an integer as a SmallInteger (or `LargeInteger` past 56 bits); a float, a string and a large integer allocate a new object on every execution; a symbol is the interned symbol; a char a `Character`; the unset marker the bootstrap singleton. | number, string, symbol and character literals; the elements of `#(...)`; the selector of a method install; the call-form prologue's unset marker |
| `PUSH_NIL` | 8 | — | `-> nil` | | `nil`; the value of an empty body, of an inlined `ifTrue:` not taken and of an inlined loop; top-level temporaries |
| `PUSH_TRUE` | 9 | — | `-> true` | | `true` |
| `PUSH_FALSE` | 10 | — | `-> false` | | `false` |
| `PUSH_SELF` | 6 | — | `-> self` | Pushes the frame's header `self` (the receiver in a method, `__block_self__` in a block). | `self` |
| `MAKE_ARRAY` | 29 | element count (wide) | `v1..vn -> array` | Pops the n values (the first pushed is element 1) and pushes a new `Array` (allocates; the elements stay in their slots until the array exists). | `#( ... )` and `{ ... }`; both build a fresh array on every evaluation |

### 2.2 Locals and the operand stack

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `PUSH_LOCAL` | 2 | slot (wide) | `-> v` | Pushes the frame slot. Errors: `engine local slot out of range`. | a temporary or argument not captured; `self` at the end of a method (`PUSH_LOCAL 0`); the prologue copy of a captured argument |
| `STORE_LOCAL` | 3 | slot (wide) | `v ->` | Pops into the slot. Errors: `STORE_LOCAL with empty stack`. | assignment to a temporary (after `DUP`); the hidden locals of `to:do:`, `doYielding:` and cascades; the call-form default prologue |
| `DUP` | 4 | — | `v -> v v` | | every assignment (the assigned value is the expression's value); each part of a cascade; a class declaration |
| `POP` | 5 | — | `v ->` | | between statements; after a cascade part; after an inlined loop body |

### 2.3 Globals, instance and class variables

A global is an attribute of the runtime's mutable globals object, keyed by
an interned symbol. An instance variable `v` is the attribute `_iv_v` of
`self` (the prefix keeps it apart from a method `v`); a class variable is
the attribute `_iv_v` of the class that declares it.

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `PUSH_GLOBAL` | 24 | name constant (wide) | `-> v` | Pushes the global. Errors: `Error` "undefined global: X" (catchable) when it is unbound or bound to nil. | a capitalised or otherwise unresolved name; the class in a declaration or method install |
| `STORE_GLOBAL` | 25 | name constant (wide) | `v ->` | Pops and binds the global. | a class declaration (after `DUP`); top-level assignment in the REPL |
| `PUSH_INSTVAR` | 26 | name constant (wide) | `-> v` | Pushes `self`'s `_iv_<name>`, found up the prototype chain (so an instance reads a class variable from its class); nil when absent. | an instance variable or class variable in an instance-side method; a class variable in a class-side method |
| `STORE_INSTVAR` | 27 | name constant (wide) | `v ->` | Pops and sets `self`'s own `_iv_<name>` (publishes a new version of `self` in protoCore's mutable table). Errors: `STORE_INSTVAR self is null`. | assignment to an instance variable that is not part of a write group |
| `PUSH_OWN_INSTVAR` | 42 | name constant (wide) | `-> v` | As `PUSH_INSTVAR`, own attribute only (a subclass does not see its superclass's value). | a class-instance variable in a class-side method |
| `STORE_CLASSVAR` | 41 | name constant (wide) | `v ->` | Pops and writes `_iv_<name>` on the first object up `self`'s chain that owns it (the declaring class), so the class, its subclasses and their instances share it. | assignment to a class variable, from either side |
| `IVAR_GROUP` | 43 | write-group entry (wide) | `v -> v` | A write of a run of instance-variable assignments, not its last: the value stays on the stack, not yet written. At the run's first write the engine decides whether to group (not when a debugger is attached); when it does not, each write is done at once as `STORE_INSTVAR`. | `tryEmitInstVarGroup`: a run of 2-16 statements `v := e` on instance variables whose values after the first cannot run code (example 16) |
| `IVAR_GROUP_END` | 44 | entry of the run's last write (wide) | `v1..vn -> vn` | Publishes the whole run as **one** new version of `self` (`ProtoObject::setAttributes`), atomic to other threads, then leaves only the last value. If a `BIN_INT_*` inside the run missed its fast path, the pending writes were flushed one by one before its send and the rest of the run is per write. Errors: `IVAR_GROUP_END stack underflow`. | the last statement of such a run. `PROTOST_IVAR_GROUPS=off` compiles runs as plain `STORE_INSTVAR`s |

### 2.4 Captured variables

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `MAKE_CAPTURED` | 28 | 0 or 1 | `->` | Installs a new captured dict in the frame's header slot: 0, a root dict (child of `Object`); 1, a child of the dict the block inherited. Allocates. | the prologue of a method whose variables blocks capture (0); of a block that declares captured variables (1) (1.4) |
| `DEFINE_CAPTURED` | 40 | name constant (wide) | `v ->` | Pops and binds the name in the current activation's own dict (shadowing an enclosing one). Errors: `DEFINE_CAPTURED without captured dict`. | the capture prologue, once per captured argument or temporary |
| `PUSH_CAPTURED` | 22 | name constant (wide) | `-> v` | Pushes the variable, found up the dict chain; nil when absent or when the frame has no dict. | a read of a captured variable |
| `STORE_CAPTURED` | 23 | name constant (wide) | `v ->` | Pops and writes the variable in the innermost dict that owns it (the current one if none does). Errors: `STORE_CAPTURED without captured dict`. | an assignment to a captured variable (after `DUP`) |

### 2.5 Control flow

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `JUMP` | 15 | forward offset | `->` | `next += operand` words. | inlined `ifTrue:ifFalse:` (over the else branch), `ifTrue:` (over the `PUSH_NIL`) |
| `JUMP_IF_FALSE` | 17 | forward offset | `v ->` | Pops; jumps when the value is exactly `false`. Any other value falls through, hence the guard below. | inlined `ifTrue:`, `ifTrue:ifFalse:`, `whileTrue:`, `to:do:` (loop exit), `doYielding:`; the call-form default prologue |
| `JUMP_IF_TRUE` | 16 | forward offset | `v ->` | Pops; jumps when the value is exactly `true`. | inlined `ifFalse:`, `whileFalse:` |
| `JUMP_BACK` | 30 | backward offset | `->` | `next -= operand` words, then polls the GC safepoint (1.6). | the back edge of inlined `whileTrue:`, `whileFalse:`, `to:do:` and of `doYielding:` |
| `ASSERT_BOOL_OR_DNU` | 38 | selector constant (wide) | `v -> v` | Leaves a Boolean in place. Any other value is popped and `doesNotUnderstand: <selector>` is signalled with it as the receiver, as the send the inlining replaced would have done. | before every conditional jump of an inlined `ifTrue:` / `ifFalse:` / `ifTrue:ifFalse:` / `ifFalse:ifTrue:` / `whileTrue:` / `whileFalse:` |

### 2.6 Sends and returns

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `SEND_UNARY` | 11 | selector constant (wide) | `r -> v` | Sends with no argument (1.5). | unary messages |
| `SEND_BINARY` | 12 | selector constant (wide) | `r a -> v` | Sends with one argument. Also the slow path of every `BIN_INT_*`. | binary messages other than `+ - < <= > >= =` (`*`, `,`, `==`, `//`, ...); `==` in the call-form prologue; `<=` and `+` in the `doYielding:` loop |
| `SEND_KEYWORD` | 13 | selector constant (wide) | `r a1..an -> v` | Sends with n = number of colons, any number. | keyword messages not inlined; class declarations and method installs (`__subclassNamed:`, `__installMethod:as:`, ...) |
| `SEND_SUPER` | 14 | selector constant (wide) | `self a1..an -> v` | A send whose lookup starts above the class that defines the running method (1.5, step 3); n is the number of colons, or 1 for a binary selector. Only this send is affected: a send in its arguments is ordinary. Errors (catchable): `super send outside a method body`, `super: cannot resolve defining class C`, `super: class C has no superclass`. | a unary, binary or keyword message, or every part of a cascade, whose receiver is `super`, after `PUSH_SELF` (example 17). `super` as a value is `PUSH_SELF`; `super name(...)` is a compile error |
| `SEND_CALL` | 39 | mangled call selector constant (wide) | `r p1..pn k1..km -> v` | Call-form send (1.5). Errors (catchable): more than 16 values; actor receiver; not a call-form method; wrong positional count; an unknown named argument; named arguments to a primitive. | `recv name(...)` (example 08) |
| `RETURN` | 18 | — | `v ->` | Returns the top of the stack (nil if empty) from the frame's home: a local return in a method or top-level frame, a non-local return in a block frame (1.7). | `^ expr`; the implicit `^ self` at the end of a method body |
| `RETURN_TOP` | 19 | — | `v ->` | Returns the top of the stack (nil if empty) from this frame only. Falling off the end of the code does the same. | the end of every block body and of the top-level module |

### 2.7 SmallInteger fast paths

| Op | # | Operand | Stack | Fast path |
|---|---|---|---|---|
| `BIN_INT_ADD` | 31 | selector constant `#+` | `a b -> a+b` | both SmallIntegers and the sum stays in range |
| `BIN_INT_SUB` | 32 | selector constant `#-` | `a b -> a-b` | both SmallIntegers and the difference stays in range |
| `BIN_INT_LE` | 33 | `#<=` | `a b -> bool` | both SmallIntegers |
| `BIN_INT_LT` | 34 | `#<` | `a b -> bool` | both SmallIntegers |
| `BIN_INT_GE` | 35 | `#>=` | `a b -> bool` | both SmallIntegers |
| `BIN_INT_GT` | 36 | `#>` | `a b -> bool` | both SmallIntegers |
| `BIN_INT_EQ` | 37 | `#=` | `a b -> bool` | both SmallIntegers (pointer equality of the tagged values) |

Semantics, common to all seven: on the fast path the result replaces the
operands with no send, no lookup and no allocation. Otherwise (another
operand type, an overflow) the operands are pushed back and the instruction
continues as `SEND_BINARY` with the same operand, so a Float, a
`LargeInteger`, a `Fraction` or a user class defining `+` behaves exactly as
with a send; an active write group is flushed first. **Emitted by:** every
binary send whose selector is one of the seven, whatever the receiver; the
counter test and increment of an inlined `to:do:`. There is no fast path for
`*`, `//` or `\\`.

### 2.8 Blocks

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `PUSH_BLOCK` | 20 | nested-module index (wide) | `-> closure` | Creates a `BlockClosure` over the nested module, stamped with the frame's captured dict, home frame id and `self` (1.4). Allocates. | a block literal that is not inlined; a method body before its install |

### 2.9 Encoding

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `EXTEND` | 254 | the next more significant operand byte | — | Prefix: `arg = (arg << 8) \| byte` for each prefix, then the real instruction. Never dispatched. | `emitWide` for an operand above 255 (above 65,535: two prefixes) |

### 2.10 Collections and exceptions

There are no collection opcodes besides `MAKE_ARRAY`: `Dictionary`,
`OrderedCollection`, `Set` and the rest are kernel classes and primitives
reached by sends (examples 10, 11). There are no exception opcodes (1.7).

## 3. Worked examples

Each example is a file in `docs/vm-examples/`; the listing after it is that
file's golden output: the bytecode, then what the program prints. Regenerate
them with
`python3 docs/vm-examples/disasm_examples.py build/protost --update` after a
compiler change, and review the notes.

| # | Concept | Example |
|---|---|---|
| 01 | constant and arithmetic | `1 + 2` |
| 02 | local binding | top-level temporaries `\| x y \|` |
| 03 | conditionals | `ifTrue:ifFalse:`, `ifTrue:` |
| 04 | define and call | a class, a method, a send |
| 05 | recursion | `fib:` |
| 06 | loops | `to:do:`, `whileTrue:` |
| 07 | closure capturing a variable | a block capturing a method argument |
| 08 | arities and defaults | keyword selectors of different arity; call-form method with a default |
| 09 | destructuring | none in Smalltalk: brace array and `at:` |
| 10 | keyed lookup | `Dictionary at:`, `at:ifAbsent:` |
| 11 | primitive call | `size`, `at:` |
| 12 | higher-order | `inject:into:` with a block |
| 13 | exceptions | `on:do:`, `signal:`, `ensure:` |
| 14 | metaprogramming | compile-time inlining of `ifTrue:`, and the send it replaces |
| 15 | laziness | a block as a deferred value; `doYielding:` |
| 16 | shared mutable state | instance variables, a write group |
| 17 | misc. | cascade, `super` |
| 18 | non-local return | `^` from a block inside `do:` |

### Example 01: a constant and arithmetic, `1 + 2`

<!-- vm-example: 01-arithmetic -->
```smalltalk
(1 + 2) printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 2)
  constants:
    #0   integer   1
    #1   integer   2
    #2   symbol    #+
    #3   symbol    #printNl
  code:
      0  PUSH_CONST         0     ; 1
      1  PUSH_CONST         1     ; 2
      2  BIN_INT_ADD        2     ; #+
      3  SEND_UNARY         3     ; #printNl
      4  RETURN_TOP
3
```

- `0`-`1` push the two integer constants.
- `2 BIN_INT_ADD 2`: both operands are SmallIntegers, so the sum is computed
  inline. The operand is the selector `#+`, used only if the fast path
  misses.
- `3 SEND_UNARY 3` sends `printNl`, a primitive that prints `3`.
- `4 RETURN_TOP` ends the top level with the value of the last statement.

### Example 02: temporaries

<!-- vm-example: 02-temporaries -->
```smalltalk
| x y |
x := 10.
y := x * 2.
(x + y) printNl.
```

```text
module main: <module>
  args 0, locals 2 (0 x, 1 y), stack 48 (deepest 2)
  constants:
    #0   integer   10
    #1   integer   2
    #2   symbol    #*
    #3   symbol    #+
    #4   symbol    #printNl
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; x
      3  POP
      4  PUSH_NIL
      5  DUP
      6  STORE_LOCAL        1     ; y
      7  POP
      8  PUSH_CONST         0     ; 10
      9  DUP
     10  STORE_LOCAL        0     ; x
     11  POP
     12  PUSH_LOCAL         0     ; x
     13  PUSH_CONST         1     ; 2
     14  SEND_BINARY        2     ; #*
     15  DUP
     16  STORE_LOCAL        1     ; y
     17  POP
     18  PUSH_LOCAL         0     ; x
     19  PUSH_LOCAL         1     ; y
     20  BIN_INT_ADD        3     ; #+
     21  SEND_UNARY         4     ; #printNl
     22  RETURN_TOP
30
```

- `0`-`7`: the parser turns `| x y |` into `x := nil. y := nil.`. Every
  assignment is `<value> DUP STORE_LOCAL` and, as a statement, `POP`: the
  `DUP` keeps the assigned value as the expression's value.
- Top-level variables are slots of the top-level frame (`locals 2`).
- `14 SEND_BINARY 2`: `*` has no fast path; it is an ordinary send.
- `20 BIN_INT_ADD`: `+` does.

### Example 03: `ifTrue:ifFalse:` and `ifTrue:`

<!-- vm-example: 03-conditionals -->
```smalltalk
| n |
n := 7.
(n > 5 ifTrue: ['big'] ifFalse: ['small']) printNl.
n odd ifTrue: ['odd' printNl].
```

```text
module main: <module>
  args 0, locals 1 (0 n), stack 48 (deepest 2)
  constants:
    #0   integer   7
    #1   integer   5
    #2   symbol    #>
    #3   symbol    #ifTrue:ifFalse:
    #4   string    'big'
    #5   string    'small'
    #6   symbol    #printNl
    #7   symbol    #odd
    #8   symbol    #ifTrue:
    #9   string    'odd'
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; n
      3  POP
      4  PUSH_CONST         0     ; 7
      5  DUP
      6  STORE_LOCAL        0     ; n
      7  POP
      8  PUSH_LOCAL         0     ; n
      9  PUSH_CONST         1     ; 5
     10  BIN_INT_GT         2     ; #>
     11  ASSERT_BOOL_OR_DNU 3     ; #ifTrue:ifFalse:
     12  JUMP_IF_FALSE      2     ; -> 15
     13  PUSH_CONST         4     ; 'big'
     14  JUMP               1     ; -> 16
     15  PUSH_CONST         5     ; 'small'
     16  SEND_UNARY         6     ; #printNl
     17  POP
     18  PUSH_LOCAL         0     ; n
     19  SEND_UNARY         7     ; #odd
     20  ASSERT_BOOL_OR_DNU 8     ; #ifTrue:
     21  JUMP_IF_FALSE      3     ; -> 25
     22  PUSH_CONST         9     ; 'odd'
     23  SEND_UNARY         6     ; #printNl
     24  JUMP               1     ; -> 26
     25  PUSH_NIL
     26  RETURN_TOP
'big'
'odd'
```

- Both conditionals are inlined: their literal blocks have no arguments and
  no temporaries, so no closure is built and nothing is sent.
- `11 ASSERT_BOOL_OR_DNU` checks that the receiver of `ifTrue:ifFalse:` is a
  Boolean; `JUMP_IF_FALSE` alone would treat `7` as true.
- `12`-`15`: false jumps to the else branch at `15`; the then branch jumps
  over it to `16`.
- `20`-`25`: a one-armed `ifTrue:` answers nil when not taken, so the
  compiler adds `25 PUSH_NIL` and a `JUMP` over it.

### Example 04: a class, a method and a send

<!-- vm-example: 04-method-and-send -->
```smalltalk
Object subclass: #Greeter
  instanceVariableNames: 'name'.

Greeter >> name: aString
  name := aString.

Greeter >> greeting
  ^ 'Hello, ', name.

(Greeter new name: 'Ada') greeting printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 3)
  constants:
    #0   symbol    #Greeter
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Greeter'
    #4   symbol    #__setClassName:
    #5   string    'Greeter'
    #6   string    'name'
    #7   symbol    #__setInstVarNames:
    #8   symbol    #name:
    #9   symbol    #__installMethod:as:
    #10  symbol    #greeting
    #11  symbol    #new
    #12  string    'Ada'
    #13  symbol    #printNl
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Greeter'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Greeter'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  PUSH_CONST         6     ; 'name'
      6  SEND_KEYWORD       7     ; #__setInstVarNames:, 1 arg
      7  DUP
      8  STORE_GLOBAL       0     ; Greeter
      9  POP
     10  PUSH_GLOBAL        0     ; Greeter
     11  PUSH_BLOCK         0     ; main.0 (Greeter>>name:)
     12  PUSH_CONST         8     ; #name:
     13  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     14  POP
     15  PUSH_GLOBAL        0     ; Greeter
     16  PUSH_BLOCK         1     ; main.1 (Greeter>>greeting)
     17  PUSH_CONST         10    ; #greeting
     18  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     19  POP
     20  PUSH_GLOBAL        0     ; Greeter
     21  SEND_UNARY         11    ; #new
     22  PUSH_CONST         12    ; 'Ada'
     23  SEND_KEYWORD       8     ; #name:, 1 arg
     24  SEND_UNARY         10    ; #greeting
     25  SEND_UNARY         13    ; #printNl
     26  RETURN_TOP

module main.0: Greeter>>name:
  args 2, locals 2 (0 self, 1 aString), stack 48 (deepest 2)
  defining class Greeter
  constants:
    #0   symbol    #name
  code:
      0  PUSH_LOCAL         1     ; aString
      1  DUP
      2  STORE_INSTVAR      0     ; name
      3  POP
      4  PUSH_LOCAL         0     ; self
      5  RETURN

module main.1: Greeter>>greeting
  args 1, locals 1 (0 self), stack 48 (deepest 2)
  defining class Greeter
  constants:
    #0   string    'Hello, '
    #1   symbol    #name
    #2   symbol    #,
  code:
      0  PUSH_CONST         0     ; 'Hello, '
      1  PUSH_INSTVAR       1     ; name
      2  SEND_BINARY        2     ; #,
      3  RETURN
'Hello, Ada'
```

- `0`-`9` declare the class at run time: `Object __subclassNamed: 'Greeter'`
  makes the class, `__setClassName:` and `__setInstVarNames:` record its name
  and instance variables, `STORE_GLOBAL` binds it.
- `10`-`13` install `name:`: the class, a closure over `main.0`, the selector
  symbol, `SEND_KEYWORD #__installMethod:as:`. The same for `greeting` at
  `15`-`18`.
- `main.0`: `args 2` is `self` and `aString`; slot 0 is `self`. The
  assignment to the instance variable is `STORE_INSTVAR`. A method without
  `^` answers `self`: `4 PUSH_LOCAL 0`, `5 RETURN`.
- `main.1`: `PUSH_INSTVAR` reads `_iv_name` of `self`; `,` is an ordinary
  binary send.

### Example 05: a recursive method, `fib:`

<!-- vm-example: 05-fib -->
```smalltalk
Object subclass: #Math.

Math >> fib: n
  n < 2 ifTrue: [ ^ n ].
  ^ (self fib: n - 1) + (self fib: n - 2).

(Math new fib: 10) printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 3)
  constants:
    #0   symbol    #Math
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Math'
    #4   symbol    #__setClassName:
    #5   string    'Math'
    #6   symbol    #fib:
    #7   symbol    #__installMethod:as:
    #8   symbol    #new
    #9   integer   10
    #10  symbol    #printNl
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Math'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Math'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  DUP
      6  STORE_GLOBAL       0     ; Math
      7  POP
      8  PUSH_GLOBAL        0     ; Math
      9  PUSH_BLOCK         0     ; main.0 (Math>>fib:)
     10  PUSH_CONST         6     ; #fib:
     11  SEND_KEYWORD       7     ; #__installMethod:as:, 2 args
     12  POP
     13  PUSH_GLOBAL        0     ; Math
     14  SEND_UNARY         8     ; #new
     15  PUSH_CONST         9     ; 10
     16  SEND_KEYWORD       6     ; #fib:, 1 arg
     17  SEND_UNARY         10    ; #printNl
     18  RETURN_TOP

module main.0: Math>>fib:
  args 2, locals 2 (0 self, 1 n), stack 48 (deepest 4)
  defining class Math
  constants:
    #0   integer   2
    #1   symbol    #<
    #2   symbol    #ifTrue:
    #3   integer   1
    #4   symbol    #-
    #5   symbol    #fib:
    #6   integer   2
    #7   symbol    #+
  code:
      0  PUSH_LOCAL         1     ; n
      1  PUSH_CONST         0     ; 2
      2  BIN_INT_LT         1     ; #<
      3  ASSERT_BOOL_OR_DNU 2     ; #ifTrue:
      4  JUMP_IF_FALSE      3     ; -> 8
      5  PUSH_LOCAL         1     ; n
      6  RETURN
      7  JUMP               1     ; -> 9
      8  PUSH_NIL
      9  POP
     10  PUSH_SELF
     11  PUSH_LOCAL         1     ; n
     12  PUSH_CONST         3     ; 1
     13  BIN_INT_SUB        4     ; #-
     14  SEND_KEYWORD       5     ; #fib:, 1 arg
     15  PUSH_SELF
     16  PUSH_LOCAL         1     ; n
     17  PUSH_CONST         6     ; 2
     18  BIN_INT_SUB        4     ; #-
     19  SEND_KEYWORD       5     ; #fib:, 1 arg
     20  BIN_INT_ADD        7     ; #+
     21  RETURN
55
```

- `main.0` `0`-`8`: the guard `n < 2 ifTrue: [ ^ n ]` is inlined. Its `^ n` is
  a `RETURN` in the method's own frame: a plain return, no unwinding. Words
  `7`-`9` (`JUMP`, `PUSH_NIL`, `POP`) complete the inlined `ifTrue:` as an
  expression statement; after a `RETURN` the `JUMP` is unreachable.
- `10`-`19`: each recursive call is `PUSH_SELF`, the argument (`BIN_INT_SUB`)
  and `SEND_KEYWORD #fib:`. The send pushes a new frame on the same engine
  (no C++ recursion) and polls the safepoint at call entry.
- `20 BIN_INT_ADD` adds the two results, `21 RETURN` answers the sum.

### Example 06: `to:do:` and `whileTrue:`

<!-- vm-example: 06-loops -->
```smalltalk
| sum i |
sum := 0.
1 to: 10 do: [ :k | sum := sum + k ].
i := 0.
[ i < 3 ] whileTrue: [ i := i + 1 ].
(sum + i) printNl.
```

```text
module main: <module>
  args 0, locals 4 (0 sum, 1 i, 2 __td_i_2, 3 __td_end_2), stack 48 (deepest 3)
  slot 2 is 'k' in words 17..32
  constants:
    #0   integer   0
    #1   integer   1
    #2   integer   10
    #3   symbol    #<=
    #4   symbol    #+
    #5   integer   1
    #6   integer   0
    #7   symbol    #whileTrue:
    #8   integer   3
    #9   symbol    #<
    #10  integer   1
    #11  symbol    #printNl
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; sum
      3  POP
      4  PUSH_NIL
      5  DUP
      6  STORE_LOCAL        1     ; i
      7  POP
      8  PUSH_CONST         0     ; 0
      9  DUP
     10  STORE_LOCAL        0     ; sum
     11  POP
     12  PUSH_CONST         1     ; 1
     13  DUP
     14  STORE_LOCAL        2     ; __td_i_2
     15  PUSH_CONST         2     ; 10
     16  STORE_LOCAL        3     ; __td_end_2
     17  PUSH_LOCAL         2     ; __td_i_2
     18  PUSH_LOCAL         3     ; __td_end_2
     19  BIN_INT_LE         3     ; #<=
     20  JUMP_IF_FALSE      11    ; -> 32
     21  PUSH_LOCAL         0     ; sum
     22  PUSH_LOCAL         2     ; __td_i_2
     23  BIN_INT_ADD        4     ; #+
     24  DUP
     25  STORE_LOCAL        0     ; sum
     26  POP
     27  PUSH_LOCAL         2     ; __td_i_2
     28  PUSH_CONST         5     ; 1
     29  BIN_INT_ADD        4     ; #+
     30  STORE_LOCAL        2     ; __td_i_2
     31  JUMP_BACK          15    ; -> 17
     32  POP
     33  PUSH_CONST         6     ; 0
     34  DUP
     35  STORE_LOCAL        1     ; i
     36  POP
     37  PUSH_LOCAL         1     ; i
     38  PUSH_CONST         8     ; 3
     39  BIN_INT_LT         9     ; #<
     40  ASSERT_BOOL_OR_DNU 7     ; #whileTrue:
     41  JUMP_IF_FALSE      7     ; -> 49
     42  PUSH_LOCAL         1     ; i
     43  PUSH_CONST         10    ; 1
     44  BIN_INT_ADD        4     ; #+
     45  DUP
     46  STORE_LOCAL        1     ; i
     47  POP
     48  JUMP_BACK          12    ; -> 37
     49  PUSH_NIL
     50  POP
     51  PUSH_LOCAL         0     ; sum
     52  PUSH_LOCAL         1     ; i
     53  BIN_INT_ADD        4     ; #+
     54  SEND_UNARY         11    ; #printNl
     55  RETURN_TOP
58
```

- `12`-`16`: `to:do:` is inlined. The counter and the limit get hidden slots
  (`__td_i_2`, `__td_end_2`); the header line `slot 2 is 'k' in words 17..32`
  tells the debugger to show slot 2 as `k` inside the loop. The receiver and
  the limit are each evaluated once: `13 DUP` keeps a copy of the receiver
  under the loop as the value of the `to:do:` expression (popped at `32`,
  since the statement's value is unused).
- `17`-`20`: the loop test, `BIN_INT_LE` then `JUMP_IF_FALSE` to the exit at
  `32`. No `ASSERT_BOOL_OR_DNU` here: the test is generated by the compiler.
- `27`-`31`: `k := k + 1` with `BIN_INT_ADD`, then `JUMP_BACK 15` to `17`; the
  back edge polls the GC safepoint.
- `37`-`49`: `whileTrue:` is inlined the same way, with the guard on the
  condition at every iteration, and answers nil (`49 PUSH_NIL`).

### Example 07: a block capturing a method argument

<!-- vm-example: 07-closure -->
```smalltalk
Object subclass: #Adders.

Adders >> adderFor: n
  ^ [ :x | x + n ].

| add5 |
add5 := Adders new adderFor: 5.
(add5 value: 10) printNl.
```

```text
module main: <module>
  args 0, locals 1 (0 add5), stack 48 (deepest 3)
  constants:
    #0   symbol    #Adders
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Adders'
    #4   symbol    #__setClassName:
    #5   string    'Adders'
    #6   symbol    #adderFor:
    #7   symbol    #__installMethod:as:
    #8   symbol    #new
    #9   integer   5
    #10  integer   10
    #11  symbol    #value:
    #12  symbol    #printNl
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Adders'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Adders'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  DUP
      6  STORE_GLOBAL       0     ; Adders
      7  POP
      8  PUSH_GLOBAL        0     ; Adders
      9  PUSH_BLOCK         0     ; main.0 (Adders>>adderFor:)
     10  PUSH_CONST         6     ; #adderFor:
     11  SEND_KEYWORD       7     ; #__installMethod:as:, 2 args
     12  POP
     13  PUSH_NIL
     14  DUP
     15  STORE_LOCAL        0     ; add5
     16  POP
     17  PUSH_GLOBAL        0     ; Adders
     18  SEND_UNARY         8     ; #new
     19  PUSH_CONST         9     ; 5
     20  SEND_KEYWORD       6     ; #adderFor:, 1 arg
     21  DUP
     22  STORE_LOCAL        0     ; add5
     23  POP
     24  PUSH_LOCAL         0     ; add5
     25  PUSH_CONST         10    ; 10
     26  SEND_KEYWORD       11    ; #value:, 1 arg
     27  SEND_UNARY         12    ; #printNl
     28  RETURN_TOP

module main.0: Adders>>adderFor:
  args 2, locals 2 (0 self, 1 n), stack 48 (deepest 1)
  defining class Adders
  constants:
    #0   symbol    #n
  code:
      0  MAKE_CAPTURED      0     ; method root dict
      1  PUSH_LOCAL         1     ; n
      2  DEFINE_CAPTURED    0     ; n
      3  PUSH_BLOCK         0     ; main.0.0 ([] in Adders>>adderFor:)
      4  RETURN

module main.0.0: [] in Adders>>adderFor:
  args 1, locals 1 (0 x), stack 48 (deepest 2)
  defining class Adders
  constants:
    #0   symbol    #n
    #1   symbol    #+
  code:
      0  PUSH_LOCAL         0     ; x
      1  PUSH_CAPTURED      0     ; n
      2  BIN_INT_ADD        1     ; #+
      3  RETURN_TOP
15
```

- `main.0` (`adderFor:`): the closure analysis finds that the inner block
  uses `n`, so `n` is a captured variable. The prologue `MAKE_CAPTURED 0`
  creates the method's dict and `PUSH_LOCAL 1`, `DEFINE_CAPTURED n` copies the
  argument into it.
- `3 PUSH_BLOCK 0` creates the closure over `main.0.0`; it records the dict,
  so it can outlive the method's frame.
- `main.0.0`: `x` is the block's own argument (slot 0); `n` is read from the
  captured dict with `PUSH_CAPTURED`. `RETURN_TOP` ends a block.
- `main` `24`-`26`: `add5 value: 10` is an ordinary keyword send. The engine
  sees a block receiver and a `value:` selector and pushes a block frame
  directly (1.5 step 2).

### Example 08: selectors of different arity, and a call-form method

<!-- vm-example: 08-arity-and-defaults -->
```smalltalk
Object subclass: #Box
  instanceVariableNames: 'w h'.

Box >> w: aW
  w := aW. h := aW.

Box >> w: aW h: aH
  w := aW. h := aH.

Box >> area
  ^ w * h.

Box >> scaled(factor, offset = 0)
  ^ (w * factor) + offset.

(Box new w: 3) area printNl.
(Box new w: 3 h: 4) area printNl.
((Box new w: 2) scaled(10)) printNl.
((Box new w: 2) scaled(10, offset = 1)) printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 3)
  constants:
    #0   symbol    #Box
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Box'
    #4   symbol    #__setClassName:
    #5   string    'Box'
    #6   string    'w h'
    #7   symbol    #__setInstVarNames:
    #8   symbol    #w:
    #9   symbol    #__installMethod:as:
    #10  symbol    #w:h:
    #11  symbol    #area
    #12  symbol    #scaled
    #13  symbol    #new
    #14  integer   3
    #15  symbol    #printNl
    #16  integer   3
    #17  integer   4
    #18  integer   2
    #19  integer   10
    #20  symbol    #scaled#1
    #21  integer   2
    #22  integer   10
    #23  integer   1
    #24  symbol    #scaled#1#offset
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Box'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Box'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  PUSH_CONST         6     ; 'w h'
      6  SEND_KEYWORD       7     ; #__setInstVarNames:, 1 arg
      7  DUP
      8  STORE_GLOBAL       0     ; Box
      9  POP
     10  PUSH_GLOBAL        0     ; Box
     11  PUSH_BLOCK         0     ; main.0 (Box>>w:)
     12  PUSH_CONST         8     ; #w:
     13  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     14  POP
     15  PUSH_GLOBAL        0     ; Box
     16  PUSH_BLOCK         1     ; main.1 (Box>>w:h:)
     17  PUSH_CONST         10    ; #w:h:
     18  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     19  POP
     20  PUSH_GLOBAL        0     ; Box
     21  PUSH_BLOCK         2     ; main.2 (Box>>area)
     22  PUSH_CONST         11    ; #area
     23  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     24  POP
     25  PUSH_GLOBAL        0     ; Box
     26  PUSH_BLOCK         3     ; main.3 (Box>>scaled(...))
     27  PUSH_CONST         12    ; #scaled
     28  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     29  POP
     30  PUSH_GLOBAL        0     ; Box
     31  SEND_UNARY         13    ; #new
     32  PUSH_CONST         14    ; 3
     33  SEND_KEYWORD       8     ; #w:, 1 arg
     34  SEND_UNARY         11    ; #area
     35  SEND_UNARY         15    ; #printNl
     36  POP
     37  PUSH_GLOBAL        0     ; Box
     38  SEND_UNARY         13    ; #new
     39  PUSH_CONST         16    ; 3
     40  PUSH_CONST         17    ; 4
     41  SEND_KEYWORD       10    ; #w:h:, 2 args
     42  SEND_UNARY         11    ; #area
     43  SEND_UNARY         15    ; #printNl
     44  POP
     45  PUSH_GLOBAL        0     ; Box
     46  SEND_UNARY         13    ; #new
     47  PUSH_CONST         18    ; 2
     48  SEND_KEYWORD       8     ; #w:, 1 arg
     49  PUSH_CONST         19    ; 10
     50  SEND_CALL          20    ; call scaled#1
     51  SEND_UNARY         15    ; #printNl
     52  POP
     53  PUSH_GLOBAL        0     ; Box
     54  SEND_UNARY         13    ; #new
     55  PUSH_CONST         21    ; 2
     56  SEND_KEYWORD       8     ; #w:, 1 arg
     57  PUSH_CONST         22    ; 10
     58  PUSH_CONST         23    ; 1
     59  SEND_CALL          24    ; call scaled#1#offset
     60  SEND_UNARY         15    ; #printNl
     61  RETURN_TOP

module main.0: Box>>w:
  args 2, locals 2 (0 self, 1 aW), stack 48 (deepest 2)
  defining class Box
  write groups:
    @0   w, write 1 of 2
    @1   h, write 2 of 2
  constants:
    #0   symbol    #w
    #1   symbol    #h
  code:
      0  PUSH_LOCAL         1     ; aW
      1  IVAR_GROUP         0     ; w, write 1 of 2
      2  PUSH_LOCAL         1     ; aW
      3  IVAR_GROUP_END     1     ; h, write 2 of 2
      4  POP
      5  PUSH_LOCAL         0     ; self
      6  RETURN

module main.1: Box>>w:h:
  args 3, locals 3 (0 self, 1 aW, 2 aH), stack 48 (deepest 2)
  defining class Box
  write groups:
    @0   w, write 1 of 2
    @1   h, write 2 of 2
  constants:
    #0   symbol    #w
    #1   symbol    #h
  code:
      0  PUSH_LOCAL         1     ; aW
      1  IVAR_GROUP         0     ; w, write 1 of 2
      2  PUSH_LOCAL         2     ; aH
      3  IVAR_GROUP_END     1     ; h, write 2 of 2
      4  POP
      5  PUSH_LOCAL         0     ; self
      6  RETURN

module main.2: Box>>area
  args 1, locals 1 (0 self), stack 48 (deepest 2)
  defining class Box
  constants:
    #0   symbol    #w
    #1   symbol    #h
    #2   symbol    #*
  code:
      0  PUSH_INSTVAR       0     ; w
      1  PUSH_INSTVAR       1     ; h
      2  SEND_BINARY        2     ; #*
      3  RETURN

module main.3: Box>>scaled(...)
  args 3, locals 3 (0 self, 1 factor, 2 offset), stack 48 (deepest 2)
  defining class Box
  call form: 1 positional, offset=
  constants:
    #0   unset     <unset>
    #1   symbol    #==
    #2   integer   0
    #3   symbol    #w
    #4   symbol    #*
    #5   symbol    #+
  code:
      0  PUSH_LOCAL         2     ; offset
      1  PUSH_CONST         0     ; <unset>
      2  SEND_BINARY        1     ; #==
      3  JUMP_IF_FALSE      2     ; -> 6
      4  PUSH_CONST         2     ; 0
      5  STORE_LOCAL        2     ; offset
      6  PUSH_INSTVAR       3     ; w
      7  PUSH_LOCAL         1     ; factor
      8  SEND_BINARY        4     ; #*
      9  PUSH_LOCAL         2     ; offset
     10  BIN_INT_ADD        5     ; #+
     11  RETURN
9
12
20
21
```

- Smalltalk has no overloading: `w:` and `w:h:` are different selectors,
  hence different methods (`main.0`, `main.1`), each sent with its own
  `SEND_KEYWORD`.
- `main.0` and `main.1`: two consecutive assignments to instance variables
  whose second value cannot run code form a write group: `IVAR_GROUP`,
  `IVAR_GROUP_END` publish `w` and `h` as one new version of the object.
- `scaled(factor, offset = 0)` is a call-form method. It is installed under
  the bare name `scaled` (`main` `25`-`28`) and its module carries the
  signature (`call form: 1 positional, offset=`).
- `main` `50 SEND_CALL 20` calls `scaled#1`: one positional value, no named
  ones. The engine fills `offset` with the unset marker, and the prologue of
  `main.3` (`0`-`5`) compares it with `==` and stores the default `0`.
  `59 SEND_CALL 24` (`scaled#1#offset`) passes `offset = 1`, so the prologue
  skips the default.

### Example 09: no destructuring

Smalltalk has no destructuring assignment. A tuple is an `Array`, read with
`at:`.

<!-- vm-example: 09-no-destructuring -->
```smalltalk
| pair a b |
pair := { 3 + 4. 'seven' }.
a := pair at: 1.
b := pair at: 2.
#(1 $a 'str' #sym 2.5) size printNl.
(a printString, ' ', b) printNl.
```

```text
module main: <module>
  args 0, locals 3 (0 pair, 1 a, 2 b), stack 48 (deepest 5)
  constants:
    #0   integer   3
    #1   integer   4
    #2   symbol    #+
    #3   string    'seven'
    #4   integer   1
    #5   symbol    #at:
    #6   integer   2
    #7   integer   1
    #8   char      $a
    #9   string    'str'
    #10  symbol    #sym
    #11  float     2.5
    #12  symbol    #size
    #13  symbol    #printNl
    #14  symbol    #printString
    #15  string    ' '
    #16  symbol    #,
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; pair
      3  POP
      4  PUSH_NIL
      5  DUP
      6  STORE_LOCAL        1     ; a
      7  POP
      8  PUSH_NIL
      9  DUP
     10  STORE_LOCAL        2     ; b
     11  POP
     12  PUSH_CONST         0     ; 3
     13  PUSH_CONST         1     ; 4
     14  BIN_INT_ADD        2     ; #+
     15  PUSH_CONST         3     ; 'seven'
     16  MAKE_ARRAY         2     ; 2 elements
     17  DUP
     18  STORE_LOCAL        0     ; pair
     19  POP
     20  PUSH_LOCAL         0     ; pair
     21  PUSH_CONST         4     ; 1
     22  SEND_KEYWORD       5     ; #at:, 1 arg
     23  DUP
     24  STORE_LOCAL        1     ; a
     25  POP
     26  PUSH_LOCAL         0     ; pair
     27  PUSH_CONST         6     ; 2
     28  SEND_KEYWORD       5     ; #at:, 1 arg
     29  DUP
     30  STORE_LOCAL        2     ; b
     31  POP
     32  PUSH_CONST         7     ; 1
     33  PUSH_CONST         8     ; $a
     34  PUSH_CONST         9     ; 'str'
     35  PUSH_CONST         10    ; #sym
     36  PUSH_CONST         11    ; 2.5
     37  MAKE_ARRAY         5     ; 5 elements
     38  SEND_UNARY         12    ; #size
     39  SEND_UNARY         13    ; #printNl
     40  POP
     41  PUSH_LOCAL         1     ; a
     42  SEND_UNARY         14    ; #printString
     43  PUSH_CONST         15    ; ' '
     44  SEND_BINARY        16    ; #,
     45  PUSH_LOCAL         2     ; b
     46  SEND_BINARY        16    ; #,
     47  SEND_UNARY         13    ; #printNl
     48  RETURN_TOP
5
'7 seven'
```

- `12`-`16`: a brace array evaluates its elements (`3 + 4` with the fast
  path) and `MAKE_ARRAY 2` collects them. The header's `deepest 5` is the
  largest array in the module, whose five elements are on the stack at once.
- `32`-`37`: a literal array `#(...)` compiles the same way, from constants;
  it is a fresh array on every evaluation, not a shared literal.
- `22`, `28`: `at:` is an ordinary keyword send to a primitive.

### Example 10: keyed lookup, `Dictionary at:`

<!-- vm-example: 10-keyed-lookup -->
```smalltalk
| d |
d := Dictionary new.
d at: #k put: 42.
(d at: #k) printNl.
(d at: #missing ifAbsent: [ 0 ]) printNl.
```

```text
module main: <module>
  args 0, locals 1 (0 d), stack 48 (deepest 3)
  constants:
    #0   symbol    #Dictionary
    #1   symbol    #new
    #2   symbol    #k
    #3   integer   42
    #4   symbol    #at:put:
    #5   symbol    #at:
    #6   symbol    #printNl
    #7   symbol    #missing
    #8   symbol    #at:ifAbsent:
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; d
      3  POP
      4  PUSH_GLOBAL        0     ; Dictionary
      5  SEND_UNARY         1     ; #new
      6  DUP
      7  STORE_LOCAL        0     ; d
      8  POP
      9  PUSH_LOCAL         0     ; d
     10  PUSH_CONST         2     ; #k
     11  PUSH_CONST         3     ; 42
     12  SEND_KEYWORD       4     ; #at:put:, 2 args
     13  POP
     14  PUSH_LOCAL         0     ; d
     15  PUSH_CONST         2     ; #k
     16  SEND_KEYWORD       5     ; #at:, 1 arg
     17  SEND_UNARY         6     ; #printNl
     18  POP
     19  PUSH_LOCAL         0     ; d
     20  PUSH_CONST         7     ; #missing
     21  PUSH_BLOCK         0     ; main.0 (<block>)
     22  SEND_KEYWORD       8     ; #at:ifAbsent:, 2 args
     23  SEND_UNARY         6     ; #printNl
     24  RETURN_TOP

module main.0: <block>
  args 0, locals 0, stack 48 (deepest 1)
  constants:
    #0   integer   0
  code:
      0  PUSH_CONST         0     ; 0
      1  RETURN_TOP
42
0
```

- Dictionaries have no opcode: `Dictionary new`, `at:put:`, `at:` and
  `at:ifAbsent:` are sends. The symbol `#k` is one constant, used twice.
- `21 PUSH_BLOCK 0`: the `ifAbsent:` block is a real closure, passed to the
  primitive, which runs it only when the key is missing (in a nested engine).

### Example 11: a primitive call

<!-- vm-example: 11-primitive-call -->
```smalltalk
| xs |
xs := #(10 20 30).
xs size printNl.
(xs at: 2) printNl.
```

```text
module main: <module>
  args 0, locals 1 (0 xs), stack 48 (deepest 3)
  constants:
    #0   integer   10
    #1   integer   20
    #2   integer   30
    #3   symbol    #size
    #4   symbol    #printNl
    #5   integer   2
    #6   symbol    #at:
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; xs
      3  POP
      4  PUSH_CONST         0     ; 10
      5  PUSH_CONST         1     ; 20
      6  PUSH_CONST         2     ; 30
      7  MAKE_ARRAY         3     ; 3 elements
      8  DUP
      9  STORE_LOCAL        0     ; xs
     10  POP
     11  PUSH_LOCAL         0     ; xs
     12  SEND_UNARY         3     ; #size
     13  SEND_UNARY         4     ; #printNl
     14  POP
     15  PUSH_LOCAL         0     ; xs
     16  PUSH_CONST         5     ; 2
     17  SEND_KEYWORD       6     ; #at:, 1 arg
     18  SEND_UNARY         4     ; #printNl
     19  RETURN_TOP
3
20
```

- `12 SEND_UNARY #size` and `17 SEND_KEYWORD #at:` look the selector up on
  the `Array` prototype and find a primitive marker, so the engine calls the
  C++ function in the same context, with no frame (1.5 step 7).

### Example 12: `inject:into:` with a block

<!-- vm-example: 12-inject-into -->
```smalltalk
(#(1 2 3 4) inject: 0 into: [ :acc :x | acc + x ]) printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 4)
  constants:
    #0   integer   1
    #1   integer   2
    #2   integer   3
    #3   integer   4
    #4   integer   0
    #5   symbol    #inject:into:
    #6   symbol    #printNl
  code:
      0  PUSH_CONST         0     ; 1
      1  PUSH_CONST         1     ; 2
      2  PUSH_CONST         2     ; 3
      3  PUSH_CONST         3     ; 4
      4  MAKE_ARRAY         4     ; 4 elements
      5  PUSH_CONST         4     ; 0
      6  PUSH_BLOCK         0     ; main.0 (<block>)
      7  SEND_KEYWORD       5     ; #inject:into:, 2 args
      8  SEND_UNARY         6     ; #printNl
      9  RETURN_TOP

module main.0: <block>
  args 2, locals 2 (0 acc, 1 x), stack 48 (deepest 2)
  constants:
    #0   symbol    #+
  code:
      0  PUSH_LOCAL         0     ; acc
      1  PUSH_LOCAL         1     ; x
      2  BIN_INT_ADD        0     ; #+
      3  RETURN_TOP
10
```

- `6 PUSH_BLOCK 0` makes the two-argument block a closure; `inject:into:` is
  a send.
- The `inject:into:` primitive runs the block through `invokeBlock`, one nested
  engine per element; `main.0` is a plain two-argument body.

### Example 13: `on:do:`, `signal:`, `ensure:`

<!-- vm-example: 13-exceptions -->
```smalltalk
| r |
r := [ Error signal: 'boom'. 'not reached' ]
  on: Error
  do: [ :e | e messageText ].
r printNl.
[ 'body' printNl ] ensure: [ 'cleanup' printNl ].
```

```text
module main: <module>
  args 0, locals 1 (0 r), stack 48 (deepest 3)
  constants:
    #0   symbol    #Error
    #1   symbol    #on:do:
    #2   symbol    #printNl
    #3   symbol    #ensure:
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; r
      3  POP
      4  PUSH_BLOCK         0     ; main.0 (<block>)
      5  PUSH_GLOBAL        0     ; Error
      6  PUSH_BLOCK         1     ; main.1 (<block>)
      7  SEND_KEYWORD       1     ; #on:do:, 2 args
      8  DUP
      9  STORE_LOCAL        0     ; r
     10  POP
     11  PUSH_LOCAL         0     ; r
     12  SEND_UNARY         2     ; #printNl
     13  POP
     14  PUSH_BLOCK         2     ; main.2 (<block>)
     15  PUSH_BLOCK         3     ; main.3 (<block>)
     16  SEND_KEYWORD       3     ; #ensure:, 1 arg
     17  RETURN_TOP

module main.0: <block>
  args 0, locals 0, stack 48 (deepest 2)
  constants:
    #0   symbol    #Error
    #1   string    'boom'
    #2   symbol    #signal:
    #3   string    'not reached'
  code:
      0  PUSH_GLOBAL        0     ; Error
      1  PUSH_CONST         1     ; 'boom'
      2  SEND_KEYWORD       2     ; #signal:, 1 arg
      3  POP
      4  PUSH_CONST         3     ; 'not reached'
      5  RETURN_TOP

module main.1: <block>
  args 1, locals 1 (0 e), stack 48 (deepest 1)
  constants:
    #0   symbol    #messageText
  code:
      0  PUSH_LOCAL         0     ; e
      1  SEND_UNARY         0     ; #messageText
      2  RETURN_TOP

module main.2: <block>
  args 0, locals 0, stack 48 (deepest 1)
  constants:
    #0   string    'body'
    #1   symbol    #printNl
  code:
      0  PUSH_CONST         0     ; 'body'
      1  SEND_UNARY         1     ; #printNl
      2  RETURN_TOP

module main.3: <block>
  args 0, locals 0, stack 48 (deepest 1)
  constants:
    #0   string    'cleanup'
    #1   symbol    #printNl
  code:
      0  PUSH_CONST         0     ; 'cleanup'
      1  SEND_UNARY         1     ; #printNl
      2  RETURN_TOP
'boom'
'body'
'cleanup'
```

- No exception opcodes: `4`-`7` send `on:do:` to the protected block, with
  the class and the handler block as arguments. The `on:do:` primitive pushes
  a handler entry and runs `main.0` in a nested engine.
- `main.0` `2 SEND_KEYWORD #signal:`: the `signal:` primitive finds the
  handler on the thread's handler stack and runs `main.1` on top of the
  signalling frames; the handler falls off its end, so the `on:do:` answers
  its value and the frames of `main.0` are unwound by a C++ exception.
  `'not reached'` is never pushed.
- `14`-`16`: `ensure:` is a send too; the primitive runs the cleanup block
  after the body.

### Example 14: compile-time inlining, and the send it replaces

protoST has no macros. Its metaprogramming at compile time is the inlining
of the control messages; the same message with a block that is not a literal
is an ordinary send.

<!-- vm-example: 14-inlining -->
```smalltalk
| cond blk |
cond := 3 > 2.
(cond ifTrue: [ 'inlined' ]) printNl.
blk := [ 'sent' ].
(cond ifTrue: blk) printNl.
```

```text
module main: <module>
  args 0, locals 2 (0 cond, 1 blk), stack 48 (deepest 2)
  constants:
    #0   integer   3
    #1   integer   2
    #2   symbol    #>
    #3   symbol    #ifTrue:
    #4   string    'inlined'
    #5   symbol    #printNl
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; cond
      3  POP
      4  PUSH_NIL
      5  DUP
      6  STORE_LOCAL        1     ; blk
      7  POP
      8  PUSH_CONST         0     ; 3
      9  PUSH_CONST         1     ; 2
     10  BIN_INT_GT         2     ; #>
     11  DUP
     12  STORE_LOCAL        0     ; cond
     13  POP
     14  PUSH_LOCAL         0     ; cond
     15  ASSERT_BOOL_OR_DNU 3     ; #ifTrue:
     16  JUMP_IF_FALSE      2     ; -> 19
     17  PUSH_CONST         4     ; 'inlined'
     18  JUMP               1     ; -> 20
     19  PUSH_NIL
     20  SEND_UNARY         5     ; #printNl
     21  POP
     22  PUSH_BLOCK         0     ; main.0 (<block>)
     23  DUP
     24  STORE_LOCAL        1     ; blk
     25  POP
     26  PUSH_LOCAL         0     ; cond
     27  PUSH_LOCAL         1     ; blk
     28  SEND_KEYWORD       3     ; #ifTrue:, 1 arg
     29  SEND_UNARY         5     ; #printNl
     30  RETURN_TOP

module main.0: <block>
  args 0, locals 0, stack 48 (deepest 1)
  constants:
    #0   string    'sent'
  code:
      0  PUSH_CONST         0     ; 'sent'
      1  RETURN_TOP
'inlined'
'sent'
```

- `14`-`20`: `cond ifTrue: [ 'inlined' ]` is inlined (guard, jump, the body's
  constant, `PUSH_NIL` for the false case).
- `26`-`28`: `cond ifTrue: blk` sends `ifTrue:` to `true`; the Boolean
  primitive evaluates the closure `main.0` in a nested engine. Both forms
  answer the same value; only the inlined one avoids the closure, the send
  and the nested engine.

### Example 15: a deferred block, and `doYielding:`

protoST has no lazy sequences. A block is the deferred computation, run when
it is sent `value`.

<!-- vm-example: 15-deferred-blocks -->
```smalltalk
| later |
later := [ 'computed on demand' ].
later value printNl.
#(1 2 3) doYielding: [ :x | x printNl ].
```

```text
module main: <module>
  args 0, locals 5 (0 later, 1 __dy_coll_1, 2 __dy_blk_1, 3 __dy_n_1, 4 __dy_i_1), stack 48 (deepest 3)
  constants:
    #0   symbol    #value
    #1   symbol    #printNl
    #2   integer   1
    #3   integer   1
    #4   integer   2
    #5   integer   3
    #6   symbol    #size
    #7   symbol    #<=
    #8   symbol    #at:
    #9   symbol    #value:
    #10  symbol    #+
  code:
      0  PUSH_NIL
      1  DUP
      2  STORE_LOCAL        0     ; later
      3  POP
      4  PUSH_BLOCK         0     ; main.0 (<block>)
      5  DUP
      6  STORE_LOCAL        0     ; later
      7  POP
      8  PUSH_LOCAL         0     ; later
      9  SEND_UNARY         0     ; #value
     10  SEND_UNARY         1     ; #printNl
     11  POP
     12  PUSH_CONST         3     ; 1
     13  PUSH_CONST         4     ; 2
     14  PUSH_CONST         5     ; 3
     15  MAKE_ARRAY         3     ; 3 elements
     16  STORE_LOCAL        1     ; __dy_coll_1
     17  PUSH_BLOCK         1     ; main.1 (<block>)
     18  STORE_LOCAL        2     ; __dy_blk_1
     19  PUSH_LOCAL         1     ; __dy_coll_1
     20  SEND_UNARY         6     ; #size
     21  STORE_LOCAL        3     ; __dy_n_1
     22  PUSH_CONST         2     ; 1
     23  STORE_LOCAL        4     ; __dy_i_1
     24  PUSH_LOCAL         4     ; __dy_i_1
     25  PUSH_LOCAL         3     ; __dy_n_1
     26  SEND_BINARY        7     ; #<=
     27  JUMP_IF_FALSE      11    ; -> 39
     28  PUSH_LOCAL         2     ; __dy_blk_1
     29  PUSH_LOCAL         1     ; __dy_coll_1
     30  PUSH_LOCAL         4     ; __dy_i_1
     31  SEND_KEYWORD       8     ; #at:, 1 arg
     32  SEND_KEYWORD       9     ; #value:, 1 arg
     33  POP
     34  PUSH_LOCAL         4     ; __dy_i_1
     35  PUSH_CONST         2     ; 1
     36  SEND_BINARY        10    ; #+
     37  STORE_LOCAL        4     ; __dy_i_1
     38  JUMP_BACK          15    ; -> 24
     39  PUSH_LOCAL         1     ; __dy_coll_1
     40  RETURN_TOP

module main.0: <block>
  args 0, locals 0, stack 48 (deepest 1)
  constants:
    #0   string    'computed on demand'
  code:
      0  PUSH_CONST         0     ; 'computed on demand'
      1  RETURN_TOP

module main.1: <block>
  args 1, locals 1 (0 x), stack 48 (deepest 1)
  constants:
    #0   symbol    #printNl
  code:
      0  PUSH_LOCAL         0     ; x
      1  SEND_UNARY         0     ; #printNl
      2  RETURN_TOP
'computed on demand'
1
2
3
```

- `4`-`10`: the block is stored, then evaluated by `SEND_UNARY #value`,
  which pushes a block frame.
- `12`-`39`: `doYielding:` with a literal block compiles to a loop over
  `size` and `at:` that sends `value:` to the block (hoisted once into
  `__dy_blk_1`). Unlike `do:`, whose primitive runs the block in a nested
  engine, the `value:` send runs it in this engine, so a `Future>>wait` in
  the body suspends the actor cooperatively. The loop uses `SEND_BINARY` for
  `<=` and `+` rather than the fast-path opcodes. The expression's value is
  the collection (`39 PUSH_LOCAL 1`).

### Example 16: instance state, and a write group

<!-- vm-example: 16-instance-state -->
```smalltalk
Object subclass: #Point3
  instanceVariableNames: 'x y z'.

Point3 >> setX: ax y: ay
  x := ax.
  y := ay.
  z := 0.

Point3 >> sum
  ^ x + y + z.

(Point3 new setX: 1 y: 2) sum printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 3)
  constants:
    #0   symbol    #Point3
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Point3'
    #4   symbol    #__setClassName:
    #5   string    'Point3'
    #6   string    'x y z'
    #7   symbol    #__setInstVarNames:
    #8   symbol    #setX:y:
    #9   symbol    #__installMethod:as:
    #10  symbol    #sum
    #11  symbol    #new
    #12  integer   1
    #13  integer   2
    #14  symbol    #printNl
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Point3'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Point3'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  PUSH_CONST         6     ; 'x y z'
      6  SEND_KEYWORD       7     ; #__setInstVarNames:, 1 arg
      7  DUP
      8  STORE_GLOBAL       0     ; Point3
      9  POP
     10  PUSH_GLOBAL        0     ; Point3
     11  PUSH_BLOCK         0     ; main.0 (Point3>>setX:y:)
     12  PUSH_CONST         8     ; #setX:y:
     13  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     14  POP
     15  PUSH_GLOBAL        0     ; Point3
     16  PUSH_BLOCK         1     ; main.1 (Point3>>sum)
     17  PUSH_CONST         10    ; #sum
     18  SEND_KEYWORD       9     ; #__installMethod:as:, 2 args
     19  POP
     20  PUSH_GLOBAL        0     ; Point3
     21  SEND_UNARY         11    ; #new
     22  PUSH_CONST         12    ; 1
     23  PUSH_CONST         13    ; 2
     24  SEND_KEYWORD       8     ; #setX:y:, 2 args
     25  SEND_UNARY         10    ; #sum
     26  SEND_UNARY         14    ; #printNl
     27  RETURN_TOP

module main.0: Point3>>setX:y:
  args 3, locals 3 (0 self, 1 ax, 2 ay), stack 48 (deepest 3)
  defining class Point3
  write groups:
    @0   x, write 1 of 3
    @1   y, write 2 of 3
    @2   z, write 3 of 3
  constants:
    #0   symbol    #x
    #1   symbol    #y
    #2   integer   0
    #3   symbol    #z
  code:
      0  PUSH_LOCAL         1     ; ax
      1  IVAR_GROUP         0     ; x, write 1 of 3
      2  PUSH_LOCAL         2     ; ay
      3  IVAR_GROUP         1     ; y, write 2 of 3
      4  PUSH_CONST         2     ; 0
      5  IVAR_GROUP_END     2     ; z, write 3 of 3
      6  POP
      7  PUSH_LOCAL         0     ; self
      8  RETURN

module main.1: Point3>>sum
  args 1, locals 1 (0 self), stack 48 (deepest 2)
  defining class Point3
  constants:
    #0   symbol    #x
    #1   symbol    #y
    #2   symbol    #+
    #3   symbol    #z
  code:
      0  PUSH_INSTVAR       0     ; x
      1  PUSH_INSTVAR       1     ; y
      2  BIN_INT_ADD        2     ; #+
      3  PUSH_INSTVAR       3     ; z
      4  BIN_INT_ADD        2     ; #+
      5  RETURN
3
```

- `main.0`: `x := ax. y := ay. z := 0.` is one write group. The values stay
  on the operand stack (`deepest 3`) and `IVAR_GROUP_END` publishes the
  three variables as a single new version of the object: another thread
  sees all three writes or none.
- `main.1`: each instance-variable read is `PUSH_INSTVAR`.

### Example 17: a cascade and `super`

<!-- vm-example: 17-cascade-super -->
```smalltalk
Object subclass: #Animal.
Animal >> describe
  ^ 'an animal'.

Animal subclass: #Dog.
Dog >> describe
  ^ super describe, ' that barks'.

| oc |
oc := OrderedCollection new.
oc add: 1; add: 2; yourself.
oc size printNl.
Dog new describe printNl.
```

```text
module main: <module>
  args 0, locals 2 (0 oc, 1 __cascade_tmp_1), stack 48 (deepest 3)
  constants:
    #0   symbol    #Animal
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Animal'
    #4   symbol    #__setClassName:
    #5   string    'Animal'
    #6   symbol    #describe
    #7   symbol    #__installMethod:as:
    #8   symbol    #Dog
    #9   string    'Dog'
    #10  string    'Dog'
    #11  symbol    #OrderedCollection
    #12  symbol    #new
    #13  integer   1
    #14  symbol    #add:
    #15  integer   2
    #16  symbol    #yourself
    #17  symbol    #size
    #18  symbol    #printNl
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Animal'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Animal'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  DUP
      6  STORE_GLOBAL       0     ; Animal
      7  POP
      8  PUSH_GLOBAL        0     ; Animal
      9  PUSH_BLOCK         0     ; main.0 (Animal>>describe)
     10  PUSH_CONST         6     ; #describe
     11  SEND_KEYWORD       7     ; #__installMethod:as:, 2 args
     12  POP
     13  PUSH_GLOBAL        0     ; Animal
     14  PUSH_CONST         9     ; 'Dog'
     15  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
     16  PUSH_CONST         10    ; 'Dog'
     17  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
     18  DUP
     19  STORE_GLOBAL       8     ; Dog
     20  POP
     21  PUSH_GLOBAL        8     ; Dog
     22  PUSH_BLOCK         1     ; main.1 (Dog>>describe)
     23  PUSH_CONST         6     ; #describe
     24  SEND_KEYWORD       7     ; #__installMethod:as:, 2 args
     25  POP
     26  PUSH_NIL
     27  DUP
     28  STORE_LOCAL        0     ; oc
     29  POP
     30  PUSH_GLOBAL        11    ; OrderedCollection
     31  SEND_UNARY         12    ; #new
     32  DUP
     33  STORE_LOCAL        0     ; oc
     34  POP
     35  PUSH_LOCAL         0     ; oc
     36  DUP
     37  PUSH_CONST         13    ; 1
     38  SEND_KEYWORD       14    ; #add:, 1 arg
     39  POP
     40  DUP
     41  PUSH_CONST         15    ; 2
     42  SEND_KEYWORD       14    ; #add:, 1 arg
     43  POP
     44  DUP
     45  SEND_UNARY         16    ; #yourself
     46  STORE_LOCAL        1     ; __cascade_tmp_1
     47  POP
     48  PUSH_LOCAL         1     ; __cascade_tmp_1
     49  POP
     50  PUSH_LOCAL         0     ; oc
     51  SEND_UNARY         17    ; #size
     52  SEND_UNARY         18    ; #printNl
     53  POP
     54  PUSH_GLOBAL        8     ; Dog
     55  SEND_UNARY         12    ; #new
     56  SEND_UNARY         6     ; #describe
     57  SEND_UNARY         18    ; #printNl
     58  RETURN_TOP

module main.0: Animal>>describe
  args 1, locals 1 (0 self), stack 48 (deepest 1)
  defining class Animal
  constants:
    #0   string    'an animal'
  code:
      0  PUSH_CONST         0     ; 'an animal'
      1  RETURN

module main.1: Dog>>describe
  args 1, locals 1 (0 self), stack 48 (deepest 2)
  defining class Dog
  constants:
    #0   symbol    #describe
    #1   string    ' that barks'
    #2   symbol    #,
  code:
      0  PUSH_SELF
      1  SEND_SUPER         0     ; #describe, 0 args
      2  PUSH_CONST         1     ; ' that barks'
      3  SEND_BINARY        2     ; #,
      4  RETURN
2
'an animal that barks'
```

- `35`-`48`: a cascade keeps the receiver on the stack: `DUP` before each
  part, `POP` after it. The last part's result goes to the hidden slot
  `__cascade_tmp_1`, the receiver is popped, and the result is pushed back
  as the cascade's value.
- `main.1` `0 PUSH_SELF`, `1 SEND_SUPER #describe`: the receiver of a super
  send is `self`; `SEND_SUPER` looks the method up from `Animal`, the
  superclass of the defining class `Dog`. Only that send is a super send: in
  `super foo: self bar`, `self bar` is an ordinary `SEND_UNARY` among the
  arguments. Every part of a cascade to `super` is a `SEND_SUPER`.

### Example 18: a non-local return from a block

<!-- vm-example: 18-non-local-return -->
```smalltalk
Object subclass: #Finder.

Finder >> firstOver: limit in: aColl
  aColl do: [ :x | x > limit ifTrue: [ ^ x ] ].
  ^ nil.

(Finder new firstOver: 3 in: #(1 2 4 8)) printNl.
```

```text
module main: <module>
  args 0, locals 0, stack 48 (deepest 6)
  constants:
    #0   symbol    #Finder
    #1   symbol    #Object
    #2   symbol    #__subclassNamed:
    #3   string    'Finder'
    #4   symbol    #__setClassName:
    #5   string    'Finder'
    #6   symbol    #firstOver:in:
    #7   symbol    #__installMethod:as:
    #8   symbol    #new
    #9   integer   3
    #10  integer   1
    #11  integer   2
    #12  integer   4
    #13  integer   8
    #14  symbol    #printNl
  code:
      0  PUSH_GLOBAL        1     ; Object
      1  PUSH_CONST         3     ; 'Finder'
      2  SEND_KEYWORD       2     ; #__subclassNamed:, 1 arg
      3  PUSH_CONST         5     ; 'Finder'
      4  SEND_KEYWORD       4     ; #__setClassName:, 1 arg
      5  DUP
      6  STORE_GLOBAL       0     ; Finder
      7  POP
      8  PUSH_GLOBAL        0     ; Finder
      9  PUSH_BLOCK         0     ; main.0 (Finder>>firstOver:in:)
     10  PUSH_CONST         6     ; #firstOver:in:
     11  SEND_KEYWORD       7     ; #__installMethod:as:, 2 args
     12  POP
     13  PUSH_GLOBAL        0     ; Finder
     14  SEND_UNARY         8     ; #new
     15  PUSH_CONST         9     ; 3
     16  PUSH_CONST         10    ; 1
     17  PUSH_CONST         11    ; 2
     18  PUSH_CONST         12    ; 4
     19  PUSH_CONST         13    ; 8
     20  MAKE_ARRAY         4     ; 4 elements
     21  SEND_KEYWORD       6     ; #firstOver:in:, 2 args
     22  SEND_UNARY         14    ; #printNl
     23  RETURN_TOP

module main.0: Finder>>firstOver:in:
  args 3, locals 3 (0 self, 1 limit, 2 aColl), stack 48 (deepest 2)
  defining class Finder
  constants:
    #0   symbol    #limit
    #1   symbol    #do:
  code:
      0  MAKE_CAPTURED      0     ; method root dict
      1  PUSH_LOCAL         1     ; limit
      2  DEFINE_CAPTURED    0     ; limit
      3  PUSH_LOCAL         2     ; aColl
      4  PUSH_BLOCK         0     ; main.0.0 ([] in Finder>>firstOver:in:)
      5  SEND_KEYWORD       1     ; #do:, 1 arg
      6  POP
      7  PUSH_NIL
      8  RETURN

module main.0.0: [] in Finder>>firstOver:in:
  args 1, locals 1 (0 x), stack 48 (deepest 2)
  defining class Finder
  constants:
    #0   symbol    #limit
    #1   symbol    #>
    #2   symbol    #ifTrue:
  code:
      0  PUSH_LOCAL         0     ; x
      1  PUSH_CAPTURED      0     ; limit
      2  BIN_INT_GT         1     ; #>
      3  ASSERT_BOOL_OR_DNU 2     ; #ifTrue:
      4  JUMP_IF_FALSE      3     ; -> 8
      5  PUSH_LOCAL         0     ; x
      6  RETURN
      7  JUMP               1     ; -> 9
      8  PUSH_NIL
      9  RETURN_TOP
4
```

- `main.0`: `limit` is used by the block, so it is captured
  (`MAKE_CAPTURED`, `DEFINE_CAPTURED`). `aColl` is not.
- `main.0.0` `6 RETURN`: `^ x` inside the block. The block runs in a nested
  engine started by the `do:` primitive, and its home (`firstOver:in:`) is in
  the outer engine, so the `RETURN` throws a C++ `NonLocalReturn`; the outer
  engine catches it, pops the method's frame and pushes `4` for the caller.
  `^ nil` at `main.0` `7`-`8` is not reached.

## 4. How to add an opcode

The model is `ASSERT_BOOL_OR_DNU` (value 38), a guard added to the inlined
conditionals. Every place to touch, in order:

1. **The enum,** `src/runtime/Opcodes.h`: the next free value (45 today),
   with a comment giving its operand and stack effect. Never renumber an
   existing opcode: the value is the encoding.
   ```cpp
   ASSERT_BOOL_OR_DNU = 38, // arg = selector const index
   ```
2. **The engine,** `ExecutionEngine::runLoop` (`src/runtime/ExecutionEngine.cpp`):
   - a `labels[]` entry for the threaded dispatch (GCC builds); a missing one
     makes the opcode fail with `unimplemented opcode (threaded dispatch)`
     on Linux only;
   - a `case Op::X: L_X: { Frame& f = frames_.back(); ... }` in the switch,
     ending with `DISPATCH_DIRECT()`, or with `DISPATCH_SCOPED()` when a local
     with a destructor (a `TransientPin`) is alive in the case's scope (a
     computed goto runs no destructor; bug S25);
   - rooting: read operands with `peek` / `opAt` and pop them once the
     result exists if anything in between allocates; pin a fresh object held
     in a C++ local across an allocation with `TransientPin`; never call
     `gcSafepoint` with a value only in a C++ local;
   - an error the program should be able to catch is signalled
     (`signalNativeError`, `signalErrorOfClass`, `doesNotUnderstand`), not
     thrown as a `std::runtime_error`.
   ```cpp
   case Op::ASSERT_BOOL_OR_DNU: L_ASSERT_BOOL_OR_DNU: {
       Frame& f = frames_.back();
       const proto::ProtoObject* v = peek(f);
       if (v == PROTO_TRUE || v == PROTO_FALSE) {
           DISPATCH_DIRECT();
       }
       const std::string& selStr = f.m->constSymbol(arg);
       const proto::ProtoObject* badRecv = pop(f);
       auto* r = doesNotUnderstand(ctx, badRecv, selStr, nullptr, 0);
       push(f, r ? r : PROTO_NONE);
       DISPATCH_DIRECT();
   }
   ```
3. **The module's scans,** `src/runtime/BytecodeModule.cpp`: if the opcode
   addresses a local slot, `cachedLocalCount` must count it (it looks only at
   `PUSH_LOCAL` / `STORE_LOCAL`); if it needs operand-stack room beyond 48,
   `cachedExtraOperandStack` must include it. A new frame field must also be
   considered by `snapshotFrames` / `restoreFrames` (cooperative yield).
4. **The compiler,** `src/frontend/Compiler.cpp`: emit it with
   `m.emitWide(Op::X, operand, currentLine_)` (or `m.emit` for an operand that
   always fits a byte). A forward jump is emitted with `m.emit(op, 0, ...)`,
   its byte position and word index (`m.instrStartPc().size()`) kept, and
   patched with `m.patchArg` once the target is known; see
   `tryEmitInlinedControl`.
   ```cpp
   auto selSym = m.internSymbol(n.text);
   m.emitWide(Op::ASSERT_BOOL_OR_DNU, static_cast<unsigned int>(selSym), currentLine_);
   ```
5. **The debugger,** `src/debugger/DebuggerRuntime.cpp`, scans code for
   `PUSH_CAPTURED` / `STORE_CAPTURED` / `DEFINE_CAPTURED` to list captured
   variables; a new opcode that touches captured variables belongs there.
6. **The disassembler,** `src/runtime/Disassembler.cpp`: a line in `opName`
   (GCC and Clang warn on an unhandled enum value) and, if it has an
   operand, a place in the `switch` of `listModule` that decodes it.
7. **Tests:** a unit test in `tests/unit/test_execution_engine.cpp` (it
   builds modules by hand), a conformance program under `tests/conformance/`
   for the behaviour (format in section 5), and a
   `docs/vm-examples/NN-name.st` whose listing shows the opcode, with its
   marker in this document. Run `disasm_examples.py build/protost --update`,
   then `ctest -R docs/vm-examples`.
8. **This document:** the opcode's row in section 2, the counts at the top
   of that section, and the notes of any example whose listing changed.

## 5. How to add a primitive

A primitive is a C++ function bound on a prototype under a selector. A send
that finds it calls it directly (1.5 step 7).

**The context rule (maintainer's rule).** A `ProtoContext` is created only
when a method, that is interpreted code with its own locals, is activated; a
primitive runs in its caller's context. protoST follows the second half:
every primitive receives the engine's `ctx` and no context is created for
it (`ExecutionEngine.cpp`, the `fn(rt_, ctx, recv, sendArgs, argcOp)` call in
the `SEND_*` case and the `fn(rt_, ctx, recv, callArgs, callNPos)` call in
`SEND_CALL`; protoCore methods are called with the same `ctx` through
`recv->call(ctx, ...)`). It departs from the first half in the other
direction: a method activation does not create a context either; methods and
blocks are frames inside one context per thread (1.3, 1.8).

1. **Write the function** with the `PrimFn` signature
   (`include/protoST/primitives.h`):
   ```cpp
   const proto::ProtoObject* prim_X(STRuntime& rt, proto::ProtoContext* ctx,
                                    const proto::ProtoObject* receiver,
                                    const proto::ProtoObject* const* args,
                                    int argc);
   ```
   `argc` is the selector's argument count, `args[0..argc-1]` the arguments.
   A wrong argument is reported by throwing `std::runtime_error` (the
   engine's `translateNativeException` turns it into a catchable `Error`
   whose `messageText` is the message), or by signalling a specific class
   (`signalErrorOfClass`). Model: the arithmetic primitives of
   `src/primitives/int_prims.cpp` (`DEFBIN`, `requireNumber`).
2. **Register and bind it** in the file's `install*Primitives` function:
   ```cpp
   bindPrimitive(rt, N, "+", reg.registerPrim(prim_NumAdd));
   ```
   `registerPrim` appends the function to the runtime's `PrimitiveRegistry`
   and answers its index; `bindPrimitive` stores the index, tagged with bit
   62, as an attribute of the prototype under the interned selector. A new
   source file needs its `install*` function declared and called in
   `STRuntime::construct` and the file listed in `CMakeLists.txt`.
3. **Running a block or a method** from a primitive: `invokeBlock(rt, ctx,
   block, args, argc)` (a nested engine) or `sendDynamic(rt, ctx, recv,
   selector, args, argc, &understood)`. Both run on the caller's `ctx`.
4. **Rooting** (protoCore EMBEDDER-CONFORMANCE rule 3): the receiver and the
   arguments are rooted by the caller's frame for the whole call. A value
   you create and keep across another allocation, or across a call that can
   run Smalltalk code, must be pinned: `TransientPin pin(ctx, obj);`, and
   `pin.reset(newObj)` when a persistent update replaces it (the
   `MAKE_ARRAY` case shows the pattern). Return the result unrooted; the
   engine pushes it into a traced slot. Never block in the kernel without an
   unmanaged region (`proto::ProtoContext::UnmanagedScope`; see `sleep:`).
5. **A Smalltalk method instead:** a primitive that needs no C++ speed or
   access belongs in the kernel (`lib/kernel/*.st`), which is ordinary
   protoST code.
6. **Test fixture.** Add `tests/conformance/<area>/<name>.st`; the glob in
   `tests/CMakeLists.txt` registers it. The first line is the directive:
   `"EXPECT: <text>"` (exit 0, last line of standard output equals the
   text), `"EXPECT-ERROR"` / `"EXPECT-ERROR: <text>"`, or `"XFAIL: ..."` for a
   known deviation (`tests/conformance/run_conformance.sh`). Cover the error
   cases too.
   ```smalltalk
   "EXPECT: 3"
   #(10 20 30) size printNl.
   ```
7. **Docs:** `docs/LANGUAGE.md` lists the built-in protocol.

## 6. Known oddities

Found while writing this document, from the code on `main` at `e2e930f`.
The five bugs found then (a `super` send with a send in its arguments, an
inlined `to:do:` evaluating its receiver twice, inlined bodies over 255
words, the fixed 48-slot operand stack, sends of more than 8 arguments) and
the dead code listed with them (four opcodes and four constant kinds never
emitted, the empty `Compiler::patchJumpToHere`, the engine's local-slot check
on the operand's low byte only) were fixed on branch `fix/vm-known-issues`
(`CHANGELOG.md`). What remains:

- **Constants are not de-duplicated** except symbols: a class declaration
  holds its name string twice (`__subclassNamed:` and `__setClassName:`),
  and example 06 holds `1` three times.
- **Unreachable code after `^` in an inlined block:** the `JUMP` that
  completes the inlined conditional is emitted after the `RETURN`
  (example 05, word 7). Harmless.
