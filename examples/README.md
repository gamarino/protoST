# protoST examples

A comprehensive set of **complete, idiomatic, runnable** protoST programs —
Track 9 of the roadmap. They range from focused single-feature illustrations
to genuine end-to-end programs, and together they cover every feature of the
language: the object model, blocks and closures, the collection hierarchy,
exceptions, non-local return, actors and futures, modules, the standard
library, and digital-twin patterns.

Every example is **executed and verified**. Each carries an
`"EXPECT: <text>"` directive on its first line — the same directive the
conformance suite uses — so the whole tree doubles as a smoke layer and is
registered with CTest (see [Running](#running) below).

> **Debugging:** these scripts can be debugged in VS Code via the protoST DAP
> adapter. See [`../docs/debugging.md`](../docs/debugging.md) for setup.

## Running

Run any single example with the `protost` binary. Examples that `Import` a
module must be run from their own directory so the bare module name resolves:

```bash
# A self-contained example — run from anywhere:
./build/protost examples/basics/01_classes_and_instances.st

# An example that imports a sibling module — run from its directory:
cd examples/modules && ../../build/protost 01_import_module.st
```

The whole set is also registered as a CTest smoke layer (one case per file).
Run it with:

```bash
ctest --test-dir build -R '^examples/' --output-on-failure
```

Files whose name starts with `_` (e.g. `modules/_geometry.st`) are importable
helper modules, not standalone examples, and are excluded from the smoke layer.

## Index

### `basics/` — the object model

| File | Shows |
|------|-------|
| `01_classes_and_instances.st` | Declaring a class with an instance variable; `new` + explicit `initialize`; sending messages. |
| `02_inheritance_and_super.st` | A two-level subclass chain; `super` reusing the inherited implementation. |
| `03_class_side_methods.st` | A class-side method acting as a custom constructor. |
| `04_mixins_and_uses.st` | Multiple inheritance via the `uses:` clause — assembling a class from two mixins. |
| `05_runtime_composition.st` | `addBehavior:` — composing behaviour into a class at runtime, no recompilation. |
| `06_printstring_override.st` | Overriding `printString` for a domain-specific rendering. |

### `blocks/` — blocks and closures

| File | Shows |
|------|-------|
| `01_closures_capture_state.st` | A closure capturing and mutating private state that outlives its method. |
| `02_higher_order.st` | Higher-order patterns — passing and returning blocks; function composition. |
| `03_control_flow_as_messages.st` | Control flow as ordinary message sends — `to:do:`, `ifTrue:ifFalse:` (FizzBuzz). |
| `04_whiletrue_loop.st` | `whileTrue:` as a message to a block (the Fibonacci sequence). |

### `collections/` — the collection hierarchy

| File | Shows |
|------|-------|
| `01_iteration_protocol.st` | The shared protocol — `select:` / `collect:` / `inject:into:` over an `Array`. |
| `02_ordered_collection.st` | `OrderedCollection` — `add:` cascades, `removeFirst` / `removeLast`. |
| `03_set_and_bag.st` | `Set` (deduplicating) versus `Bag` (counting) contrasted. |
| `04_dictionary.st` | `Dictionary` — a word-frequency count with `at:ifAbsent:` / `at:put:`. |
| `05_interval.st` | `Interval` — a lazy arithmetic sequence answering the iteration protocol. |
| `06_detect_and_count.st` | `count:` and trial-division — counting the primes below 100. |

### `exceptions/` — the exception protocol

| File | Shows |
|------|-------|
| `01_on_do_basics.st` | `on:do:` — catching a signalled exception, reading `messageText`. |
| `02_ensure_cleanup.st` | `ensure:` — a cleanup block that runs on normal exit and on unwind. |
| `03_resume_and_retry.st` | `retry` — re-running a protected block until a flaky operation succeeds. |
| `04_warning_resume.st` | `resume:` — continuing a protected computation past the signal point. |
| `05_custom_exception.st` | A user `Error` subclass; `pass` re-raising outward. |

### `nonlocal/` — non-local return

| File | Shows |
|------|-------|
| `01_early_return.st` | `^` inside a block returning straight out of a `do:` loop — a hand-written `detect:`. |
| `02_guard_clauses.st` | `^` in a method body as a guard clause — recursive factorial. |

### `actors/` — actors and futures

| File | Shows |
|------|-------|
| `01_async_and_wait.st` | `asActor`, asynchronous sends returning `Future`s, `wait`. |
| `02_future_thendo.st` | `Future` callbacks — `thenDo:` and `wait` on an actor's result. |
| `03_fan_out_fan_in.st` | The fan-out / fan-in pattern — parallel workers, results gathered. |
| `04_pipeline.st` | An actor pipeline — three stages, each its own actor. |

### `stdlib/` — the standard library

| File | Shows |
|------|-------|
| `01_stream.st` | The `stream` module — `ReadStream` / `WriteStream`. |
| `02_math.st` | The `Number` math protocol — `squared`, `sqrt`, `rounded`, `Float pi`. |
| `03_random.st` | The `random` module — a seedable, deterministic PRNG. |
| `04_json.st` | The `json` module — `JSON parse:` of a nested document. |
| `05_time.st` | The `time` module — `Time millisecondsToRun:` and `Duration`. |

### `modules/` — modules

| File | Shows |
|------|-------|
| `01_import_module.st` | `Import from:` — loading a `.st` file as a module, reaching its classes. |
| `02_subclass_imported.st` | Subclassing an imported class; `super` reaching the module's implementation. |
| `_geometry.st` | *(helper module — imported by the two examples above, not run standalone.)* |

### `programs/` — complete small programs

| File | Shows |
|------|-------|
| `calculator.st` | A recursive-descent calculator — `+ - * /` with precedence and parentheses. |
| `rpn_interpreter.st` | A tiny postfix (RPN) stack interpreter — a `Dictionary` dispatch table. |
| `monte_carlo_pi.st` | A Monte-Carlo estimate of pi — combining `random` and the math protocol. |
| `json_transform.st` | A JSON-driven data transform — parse, walk, aggregate an order document. |
| `traffic_intersection.st` | A digital-twin simulation — a traffic intersection of cooperating FSM actors. |
| `pump_twin.st` | A digital-twin simulation — an industrial pump with three parallel sensor actors. |

## The digital-twin pattern

`programs/traffic_intersection.st` and `pump_twin.st` are the two end-to-end
digital-twin demonstrations. Both express the same idea: each physical
component becomes its own actor — a finite state machine with private state,
a serialised mailbox, and independent scheduling — and a plain controller
object **fans out** messages to the component actors. Adding a component is
adding an actor; the runtime needs no changes. See the header comment of each
file for the specifics.
