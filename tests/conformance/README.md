# protoST conformance suite

> Track 6, slice 2 of the roadmap.

This is the **defensible** conformance suite for protoST. Every test here is
derived from the language reference (`docs/LANGUAGE.md`) — *not* from the
implementation. Each test answers one question: **"the language reference says
X — does protoST do X?"**

A test is a small, self-contained `.st` program. The runner executes it through
the `protost` binary as a black box (`protost <file>` in its own process),
captures the printed value and the exit code, and compares against a directive
embedded as the file's first line.

## Layout

Tests are organised by language-reference section:

```
02-lexical/           §2  lexical structure
03-grammar/           §3  grammar, precedence, cascades
04-object-model/      §4  prototypes, classes, instances, self/super
05-messages/          §5  dispatch
06-blocks/            §6  blocks and closures
07-non-local-return/  §7  non-local return
08-exceptions/        §8  the exception protocol
09-collections/       §9  the collection hierarchy
10-actors/            §10 actors and futures
11-modules/           §11 modules (each .st here; modules/ holds importable libs)
12-builtins/          §12 built-in classes and selectors
```

## The directive format

The directive **must be the first line** of the `.st` file, written as a
protoST comment (double-quoted). The runner reads it and strips the quotes.

| Directive | Meaning |
|-----------|---------|
| `"EXPECT: <text>"` | The program must succeed (exit 0) and the last non-empty line of stdout — the printed value of its final top-level expression — must equal `<text>` exactly. |
| `"EXPECT-ERROR"` | The program must fail (non-zero exit): it raises an uncaught error. |
| `"EXPECT-ERROR: <text>"` | As above, and the error output must contain `<text>`. |
| `"XFAIL: <inner-directive>"` | A test **expected to fail today**. The text after `XFAIL:` is itself an `EXPECT:`/`EXPECT-ERROR` directive describing the **spec-correct** behaviour. The runner inverts the verdict: the test "passes" when the program does **not** match the spec-correct expectation, and **fails loudly** if it unexpectedly matches (a signal the deviation was fixed — remove the `XFAIL:` marker). |

protoST prints the value of a script's last top-level statement: integers as
digits, `nil`/`true`/`false` literally, strings as their text, and any other
object as its `printString` (e.g. `a Counter`, `an Array`).

Example — a conforming test:

```smalltalk
"EXPECT: 7"
3 + 4.
```

Example — an XFAIL pinning a known deviation (§14 of `docs/LANGUAGE.md`):

```smalltalk
"XFAIL: EXPECT: true"
"§6.4 + D9: isNil is not bound. Spec-correct: nil isNil yields true."
nil isNil.
```

## XFAIL discipline

`docs/LANGUAGE.md` §14 lists the implementation's known deviations from the
spec. For each, a conformance test asserts the **spec-correct** behaviour and is
marked `XFAIL:`. These tests pin the known gaps: if an `XFAIL` test ever starts
matching the spec, the runner reports `XPASS` and fails — the marker should then
be removed.

Tests marked `XFAIL: ... NEW ...` pin discrepancies the suite discovered that
are **not** in §14 — undocumented bugs or spec imprecisions. They are listed in
the slice's delivery report.

## Running

The suite is integrated with CTest. From the project root:

```bash
ctest --test-dir build -R conformance/ --output-on-failure
```

Each `.st` file is registered as one CTest case named `conformance/<rel-path>`,
so a single failure pins a single non-conforming program. To run one file
directly:

```bash
tests/conformance/run_conformance.sh build/protost tests/conformance/03-grammar/binary-message.st
```

## Adding a test

1. Pick the section directory matching the behaviour.
2. Write a small `.st` program whose final top-level expression yields the
   value being checked.
3. Put the directive on the first line.
4. Re-run CMake configure (so the new file is discovered) and `ctest`.
