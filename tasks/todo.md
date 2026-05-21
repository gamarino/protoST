# T4-d — JSON stdlib module

## Findings
- The protoST `String` protocol is minimal: only `,`, `size`, `=`, `~=`,
  `printNl`. No `at:`, `do:`, `asArray`, `copyFrom:to:`. Character literals
  (`$a`) are 1-char Strings (F2 simplification in `STRuntime::materialize`).
- A JSON parser fundamentally needs to scan the input string char by char.
  The T4-d spec's own implementation notes ("a `ReadStream` over the input
  string", "`aChar` -> its value") assume `String at:` exists. It does not.
  This is a spec gap, not laziness on the JSON side.

## Plan
- [x] Add minimal, idiomatic String/char accessor primitives (root-cause fix):
  - `String>>at:`        — n-th character as a 1-char String (UTF-8 aware)
  - `String>>asArray`    — Array of the characters
  - `String>>asInteger`  — Unicode code point of the first character
  - `Number>>asCharacter`— code point -> a 1-char String
- [x] Write `lib/json.st` — pure-protoST recursive-descent parser + stringify.
- [x] Conformance tests under `tests/conformance/13-stdlib/json-*.st`.
- [x] Update docs/STATUS.md, the T4-d spec entry, lib/README.md.
- [x] Build + ctest green; smoke test; commit on main.

## Review
- `lib/json.st`: a recursive-descent parser (`_JSONScanner` + `JSON` class)
  and a double-dispatch stringifier. 14 conformance tests added.
- Enabling C++ changes (root-cause, not hacks): `String>>at:` / `asInteger`,
  `Number>>asCharacter` (UTF-8 codepoint primitives), `String>>size` made
  codepoint-based, and `String` / `Boolean` registered as globals.
- Full suite: 674/674 green, three consecutive runs. Smoke test OK.

## T3-a — Extensible classes from modules (Track 3)

- `subclass:` / `subclass:instanceVariableNames:` added as runtime messages on
  objectProto so any class object (incl. an imported module class reached
  through an expression) can be subclassed; the new class is bound globally
  under its name so following `>>` method definitions resolve.
- `super` resolution reworked: the defining class is now found by walking the
  receiver's prototype chain by object identity instead of re-resolving its
  name through globals — so `super` is correct across a module boundary even
  when the defining class is not a global. Global lookup kept as a fallback.
- 5 conformance tests added under tests/conformance/11-modules/ plus a
  `_extensible_counter.st` module helper. Full suite: 686/686 green (three
  runs). Smoke `pump_twin.st` => 3.
