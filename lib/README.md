# protoST Standard Library (`lib/`)

This directory holds the protoST standard library, in two parts, both plain
`.st` files:

- **`lib/kernel/`** — the kernel classes written in protoST (streams,
  `Character`, `Fraction`, `Point`, `Random`, `Time`, `Date`, `Duration`,
  `Transcript`, printing, reflection, …). The runtime loads them at start-up,
  in the order of `kernel/00-manifest.txt`; programs use them with no import.
- **`lib/*.st`** — the loadable modules, pulled in with
  `Import from: '<name>'`.

```smalltalk
m  := Import from: 'stream'.
rs := m ReadStream on: #(1 2 3).
rs next.            "=> 1"
```

## Discovery

The runtime locates `lib/` automatically — **no `STPATH` is required**. The
`STRuntime` module resolver (`findModuleFile`) consults the standard library
as the *last* step of its search order, after the cwd, `$STPATH`, and the
active venv. Because `lib/` has the lowest precedence, a user module of the
same name in any of those locations transparently shadows a stdlib module.
(The directory of the importing file is not searched.)

The `lib/` directory itself is found, first hit wins, by:

1. **`$PROTOST_LIB`** — if set and a directory, used verbatim. An explicit
   override for unusual installs or tests.
2. **Derived from the executable.** The runtime takes the directory of the
   `protost` binary (`/proc/self/exe` on Linux) and probes, in order,
   `<dir>/../share/protoST/lib` (the installed layout: `<prefix>/bin/protost`
   finds `<prefix>/share/protoST/lib`), `<dir>/share/protoST/lib` (a flat
   Windows install), then `<dir>/lib`, `<dir>/../lib` and `<dir>/../../lib`.
   A dev build runs `build/protost`, so `../lib` resolves to this project's
   `lib/`.
3. **`<cwd>/lib`** — convenient when running from a project checkout.

The same directory supplies the kernel: a `protost` paired with a `lib/` from
another version may fail to start or behave differently, so point
`PROTOST_LIB` at the `lib/` that matches the binary.

## Modules

| Module   | Imports as              | Provides                        |
|----------|-------------------------|---------------------------------|
| `json`   | `Import from: 'json'`   | `JSON` (`parse:` / `stringify:`)|
| `stream` | `Import from: 'stream'` | `ReadStream`, `WriteStream` — thin subclasses of the kernel classes |
| `random` | `Import from: 'random'` | `Random` (a fixed-seed 32-bit LCG) |
| `time`   | `Import from: 'time'`   | `Time`, `Timestamp`, `Duration` (an epoch-millisecond model) |

`stream`, `random` and `time` predate the kernel classes of the same names
and are kept so that programs written against them keep working. The classes
a module declares also become globals of the importing program, so importing
`random` or `time` replaces the kernel's `Random`, `Time` and `Duration` for
the rest of that program.

## Adding a module

1. Create `lib/<name>.st`. Define classes with the standard file-out syntax
   (`Object subclass: #Foo instanceVariableNames: '...'.` then
   `Foo >> selector ...`). See `stream.st` for the canonical shape.
2. Every non-`_`-prefixed class the module declares is exposed as an
   attribute of the module object returned by `Import from: '<name>'` — so
   `m := Import from: '<name>'. obj := m Foo new.` works. Prefix purely
   internal helper classes with `_` to keep them out of the module surface.
3. Add conformance tests under `tests/conformance/13-stdlib/`. The test
   runner globs `tests/conformance/**/*.st` automatically; files whose name
   starts with `_` are treated as importable helpers, not standalone tests.
4. Document the module in the table above.
