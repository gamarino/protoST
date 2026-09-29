# Presentable protoST for the 2026-10-15 talk — design

Date: 2026-09-28. Deadline for everything below: **Saturday 2026-10-03** (the
author is unavailable the following week). Talk: Fundación Argentina de
Smalltalk, 2026-10-15, 30–60 minutes, audience of classic Smalltalkers,
possible international reach.

## 1. Intent

**What the author said.**
- protoST is *not* pitched as a better Smalltalk and will never compete with
  the live image environment.
- It is an opportunity for Smalltalkers to read code they already know in
  applications normally inaccessible to Smalltalk, or with different
  interfaces: digital twins on the actor model, simple interfaces to other
  systems, basic interaction with other runtimes.
- No claim to know the killer application. Offer ideas, show *why it is
  possible now*, address their current pains and the worlds it opens.
- Seek enthusiasts, not masses. The audience is technical: marketing is out.
- State must be "presentable and incontestable — not complete, but without
  surprises".
- Demos live, each with a recorded fallback. Deck as a web deck with a 30-minute
  core and optional modules up to 60, plus speaker notes and a demo script.

**Success criteria.**
1. No program a Smalltalker is likely to type in the first minutes yields a
   silently wrong result. Every such case either works or fails with a clear
   error.
2. Nothing the talk shows or the docs claim is contradicted by running it.
3. No demo can crash, hang, or exhaust the laptop's memory on stage.
4. Every limitation is stated in one honest catalogue, before anyone finds it.
5. A deck, speaker notes, demo scripts, recorded fallbacks and a hard-questions
   document are ready on Saturday.

## 2. Audit baseline (2026-09-28)

Three independent audits (claims vs docs, adversarial Smalltalk semantics,
tests/CI/benchmarks/demos) against HEAD `4080dfd`. All 854 CTest cases pass;
nothing segfaulted; exceptions, non-local return, LargeInteger arithmetic,
classes with `super`, class extension and actors behave well; `pump_twin` is
deterministic and genuinely parallel (≈205 ms vs 503 ms with one worker).
The findings that block presentation are listed in §3 as work items.

## 3. Work items

Priority **P0** = must be done by the Thursday gate; **P1** = should be done,
with an explicit fallback if not; **OUT** = documented in the catalogue, not
implemented.

### 3.1 Stage safety (P0)

| ID | Problem | Required outcome |
|---|---|---|
| S1 | No GC by default: `setHeapLimits` is never called; a 3M-iteration loop reaches 7–12 GB | protoST sets default heap limits so collection runs in every normal run (value derived from the protoCore sizing rule, overridable by env var). Measured RSS of the audit loops stays bounded; runtime cost measured and reported |
| S2 | Heap hard limit aborts with SIGABRT + core dump | Out-of-memory ends the program with a clean, catchable-at-top-level error message and non-zero exit, no core dump |
| S3 | Recursion fails at 145 frames (`kSlotCapacity = 8192`), uncatchable at top level, message says nothing about recursion | Recursion depth of at least 10 000 frames for a simple method; beyond the limit, a catchable error whose message names stack depth |
| S4 | Ctrl-C kills the REPL | SIGINT interrupts the running evaluation and returns to the REPL prompt; in script mode it exits cleanly |
| S5 | Script from a pipe aborts (`ftell` on non-seekable file); unknown file reported as "Unknown option" | Scripts are read from any stream; a missing file reports "file not found: <path>" |
| S6 | An error in a fire-and-forget actor send vanishes | Unobserved actor errors are reported on stderr |

### 3.2 Silent wrong results (P0)

| ID | Problem | Required outcome |
|---|---|---|
| W1 | Subclass methods cannot see inherited instance variables (write becomes a local, read is "undefined global") | Instance variables of all superclasses are visible in subclass methods, read and write, at any depth |
| W2 | A method without a top-level `^` swallows every following statement of the script | **A blank line ends a method body** (decision of 2026-09-28). Docs, examples and tests updated; verified against all examples and conformance tests |
| W3 | An undeclared identifier in a method silently becomes a local | Compile error naming the identifier, file and line |
| W4 | A method temporary with the same name as an instance variable writes the ivar | Declaring a temporary or argument with the name of an instance variable is a compile error naming both, as in Pharo; never a silent write to the ivar |
| W5 | Blocks share variables across loop iterations and across activations of a block factory (D30 and its broader form) | Each iteration of `to:do:` / `do:` / `collect:` and each activation of a block gets fresh bindings; `mkAdder` and counter factories behave as in Pharo |
| W6 | `1.0e-10` evaluates to −7.28 (`(1.0 e) - 10`); `1.0e10`, `1e3`, `16rFF` fail | Exponent (`e`, with sign) and radix (`NrDDD`) number literals lex correctly |
| W7 | User `=`/`hash` ignored by `Set`/`Dictionary`; `~=` ignores user `=`; `#(1 2 3) = #(1 2 3)` is false | Hashed collections use `hash` and `=`; `~=` is `(self = x) not`; `Array`/`OrderedCollection` `=` compare elements |
| W8 | `'x' , 3` silently drops the argument | Error (or explicit conversion only for Strings/Symbols), never silent loss |
| W9 | `Error signal` / `Error new signal` give a garbage `messageText` | `messageText` is nil-safe and answers the class description by default, as in Pharo |
| W10 | `printString` answers `'a String'` for strings, booleans, characters; user `printOn:` never used; collections print as `an Array` | `printString`/`displayString` are implemented through `printOn:`; literals print as literals (`'abc'`, `#sym`, `true`, `$a`); collections print their elements (`#(1 2 3)`, `an OrderedCollection(1 2 3)`) |
| W11 | LargeInteger ÷ LargeInteger, `asFloat`, `sqrt` fail with "exceeds long long range" | Division, modulo, `asFloat`, `sqrt` work across the whole integer tower |
| W12 | `\\` and `//` truncate (`-7 \\ 2` = −1, `-7 // 2` = −3) | Smalltalk-80 floor semantics (`-7 // 2` = −4, `-7 \\ 2` = 1); truncating `rem:` and `quo:` added |
| W13 | Internal names leak (`true and: false` → "block missing `__bc_ptr__`"); unterminated comment accepted silently | `and:`/`or:` accept a non-block argument or report a Smalltalk-level error; unterminated comment is a syntax error |

### 3.3 Everyday protocol (P1)

Implemented in the standard library where possible (Smalltalk source), in
primitives only when needed. Each gets a conformance test.

- Object: `class` as a unary message (including `x class class` answering a
  sensible object), `yourself`, `isKindOf:`, `isMemberOf:`, `respondsTo:`,
  `perform:` (+ `with:` forms), `error:`, `subclassResponsibility`, `copy`,
  `hash`, `asString`, `displayString`, `printOn:`, `isNil`/`notNil` family,
  `->` (Association).
- Numbers: `timesRepeat:`, `rem:`, `quo:`, `printString:` / `radix:`,
  `isNumber`, `isString`, `@` only if trivially available (else OUT).
- Blocks: `valueWithArguments:`, `numArgs`.
- Collections: `first`, `last`, `includes:`, `indexOf:`, `reverse`/`reversed`,
  `copyFrom:to:`, `withIndexDo:`, `max`, `sum`/`inject:into:` helpers,
  `sort`, `sort:`, `asSortedCollection`, `asArray`, `asOrderedCollection`,
  `isEmpty`/`notEmpty`, `Dictionary add:`, `keyAtValue:`.
- Strings as collections: `do:`, `collect:`, `select:`, `reverse`,
  `asUppercase`, `asLowercase`, `indexOf:`, `copyReplaceAll:with:`, `<` family,
  `asSymbol`, `asNumber`/`asInteger` (parse, not code point), `isEmpty`.
- `WriteStream on: String new` usable to build strings; `ReadStream` basics.
- Exceptions: `ZeroDivide` and `MessageNotUnderstood` as real classes (with
  `message`, `receiver`), exception sets (`Error, ZeroDivide`), `ex return`,
  `ex description`.
- Globals: `Transcript` (`show:`, `cr`, `tab`, `space`, `print:`,
  `display:`) writing to stdout; `Smalltalk` only if trivial (else OUT).
- `new` sends `initialize` (closes D4) — P1 with a full test sweep; fallback:
  stays a documented deviation.
- Fractions: minimal `Fraction` (exact `/`, arithmetic, comparison, printing,
  `asFloat`, `numerator`/`denominator`) — P1; fallback at the Thursday gate:
  `/` keeps truncating and heads the deviation catalogue.
- Runtime errors report file:line and the method (`Class>>selector`) — P1.

### 3.4 Evidence and documentation (P0)

- **Positioning**, one wording everywhere (README, tutorial ch01, LANGUAGE,
  ROADMAP, INSTALLATION, package description): a Smalltalk-syntax,
  actor-native runtime on protoCore; a demonstrator and a base for digital
  twins; explicitly *not* a Smalltalk-80 implementation or a replacement for an
  image environment.
- **One deviation catalogue** (tutorial ch14, linked from README and
  LANGUAGE §14): every difference from Smalltalk-80 still present after §3.1–3.3,
  including OUT items (no image, no IDE, no metaclass tower, no `thisContext`,
  no `become:` if absent, top-level temps, etc.), each with a one-line reason.
- Every contradicted/stale claim from the audit fixed or removed: Future
  combinators, `yourself` examples, `self error:` examples, implicit self-send
  example, class-variable staleness, broken shell quoting, top-level temps
  example, open-bug counts, Transcript examples, collection print examples,
  `asString` advice, `ZeroDivide` claims, runtime counts, test counts,
  INSTALLATION soname/dependency facts, lock claims, K1/D2 staleness, examples
  index, arity claim, error-text samples, the pending agent decision (D32
  resolved or reworded), README comparisons with Pharo/Vats restated
  precisely or removed.
- **Interop with other runtimes**: verified end to end with a real runtime
  (protoScala publisher, and protoJS/protoPython if feasible) or the claim is
  narrowed to what is tested.
- **Benchmarks**: every benchmark checks its own result; the harness fails on a
  wrong result; CPython twins run at the same N; binary path fixed; numbers
  re-measured after the GC and closure changes and published in a dated report;
  README cites only numbers from that report, with methodology.
- STATUS, KNOWN_ISSUES, CHANGELOG consistent; version **0.4.0**; `.deb` rebuilt
  and installed; stale local branches deleted; stale `build/` directory removed
  or rebuilt so no documented command points at a broken binary.
- S18 (debugger test hangs without `< /dev/null`) fixed; S19 stays documented
  with its measured frequency.

### 3.5 The talk (P0)

Working language Spanish. Materials live in `docs/talks/2026-10-15-fas/`.

- **Deck** (web deck, exportable): a 30-minute core plus modules to 60.
  Core arc: who and why (an experiment looking for enthusiasts) → current pains
  of a Smalltalker (deployment of an image as a service, one core, integration
  with the rest of the stack, sharing code as text) framed as questions →
  why it is possible now (protoCore: immutable structures, native threads
  without a global lock, concurrent GC, several runtimes on one object model) →
  the code you know, running from a file (demo 1) → actors and real parallelism
  (demo 2) → worlds it could open (digital twins, services, interaction with
  other runtimes) as ideas, not promises → what protoST is not (the catalogue)
  → how to take part.
  Modules: protoCore internals; the digital twin walkthrough; interaction with
  other runtimes (only if verified); benchmarks and methodology; roadmap and
  open questions.
- **Demo scripts**: deterministic, each under a heap limit, each with expected
  output checked by a script; run 20× without failure before being accepted.
- **Recorded fallbacks**: `script`/`scriptreplay` recordings of every demo.
- **Speaker notes** per slide, and a **hard-questions document** with honest,
  sourced answers (Why not Pharo? Where is the image/IDE? Performance vs
  Pharo/CPython? GC? Why prototypes? Who uses it? What is missing?).
- No superlatives, no unverifiable claims; every number cites its report.

## 4. Process

- Test first for every fix (conformance `.st` or unit test), verified failing
  on the current build; full CTest (sequential, `< /dev/null`) green before
  each commit; one topic per commit on a feature branch, merged to `main`.
- Builds are sequential, never `-j` (DEV12).
- **Thursday gate**: fresh adversarial re-audit by independent agents with the
  same three briefs; any silent wrong result or stage-safety finding blocks
  the release; P1 items not done fall back as stated.
- **Saturday**: release 0.4.0 (tag, `.deb`), final re-run of every demo 20×,
  talk materials complete, handoff checklist for the author.

## 5. Out of scope

Image-based development, an IDE/browser, the metaclass tower beyond `class`,
`thisContext`, `become:`, a full Fraction/ScaledDecimal tower beyond the P1
minimum, performance work beyond S1–S3, network/HTTP libraries not already
present.
