# protoST Standard Library (`lib/`)

This directory holds the protoST standard-library modules — plain `.st` files
loadable through the module system with `Import from: '<name>'`.

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

The `lib/` directory itself is found, first hit wins, by:

1. **`$PROTOST_LIB`** — if set and a directory, used verbatim. An explicit
   override for unusual installs or tests.
2. **Derived from the executable.** The runtime reads `/proc/self/exe`, takes
   the directory of the `protost` binary, and probes `./lib`, `../lib`, and
   `../../lib` relative to it. A dev build runs `build/protost`, so `../lib`
   resolves to this project `lib/`; an installed `bin/protost` likewise finds
   `<prefix>/lib`.
3. **`<cwd>/lib`** — convenient when running from a project checkout.

## Modules

| Module   | Imports as            | Provides                          |
|----------|-----------------------|-----------------------------------|
| `stream` | `Import from: 'stream'` | `ReadStream`, `WriteStream`     |

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
