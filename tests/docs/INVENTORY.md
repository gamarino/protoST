# Documentation example inventory — 2026-09-29

> **Status (later on 2026-09-29):** every failure listed below has been fixed
> in the documents, and every document the CTest `docs/` cases run passes
> with the 0.4.0 binary (`README.md`, which was not edited in that pass, passed
> as it stood). This file is kept as the record of what was found.

This file lists every documentation example that `tests/docs/run_doc_snippets.py`
found failing. A failing example is one that did not run, or did not print the
result the document states for it. The prose is not changed here; this is the
work list for whoever fixes the documents.

## How this was measured

- **Binary:** a prebuilt `protost` built from commit `568e046`
  (`feature/presentable-0.4.0`), which is the current behaviour of the language.
  It reports `protoST 0.3.0`. Nothing was rebuilt.
- **Kernel (`PROTOST_LIB`):** `lib/` as it stands at `568e046`, exported with
  `git archive`. **The worktree's own `lib/` does not match the binary.** This
  branch is based on `4080dfd`, whose `lib/` has no `lib/kernel/` directory and
  has an older `lib/json.st`. That kernel is where the binary gets
  `printString`, `displayNl`, `Character` and the collection printing. The
  documents are identical at `4080dfd` and `568e046`, so the two commits are
  checking the same text. The section "Results with the worktree's own `lib/`"
  at the end shows what changes when you use the mismatched kernel.
- **Runs:** one run of the tool per document (the CTest shape), strictly
  sequential, with each process under `timeout 20`.

  ```
  run_doc_snippets.py -v --lib <568e046 lib> <protost-568e046> <doc>
  ```

## Counts per document

| Document | Examples | Passed | Failed | Unchecked | Skipped (`no-run`) |
|---|---:|---:|---:|---:|---:|
| README.md | 1 | 1 | 0 | 0 | 0 |
| docs/LANGUAGE.md | 24 | 17 | 7 | 37 | 0 |
| docs/TUTORIAL.md | 1 | 1 | 0 | 1 | 0 |
| docs/tutorial/01-introduction.md | 4 | 3 | 1 | 0 | 0 |
| docs/tutorial/02-objects-and-messages.md | 23 | 21 | 2 | 9 | 0 |
| docs/tutorial/03-variables-and-literals.md | 19 | 19 | 0 | 13 | 0 |
| docs/tutorial/04-blocks.md | 14 | 13 | 1 | 3 | 0 |
| docs/tutorial/05-classes-and-methods.md | 8 | 7 | 1 | 11 | 0 |
| docs/tutorial/06-non-local-return.md | 4 | 4 | 0 | 1 | 0 |
| docs/tutorial/07-exceptions.md | 8 | 7 | 1 | 3 | 0 |
| docs/tutorial/08-collections.md | 21 | 17 | 4 | 0 | 0 |
| docs/tutorial/09-standard-library.md | 8 | 4 | 4 | 1 | 0 |
| docs/tutorial/10-actors-and-futures.md | 8 | 7 | 1 | 10 | 0 |
| docs/tutorial/11-advanced-object-model.md | 4 | 3 | 1 | 1 | 0 |
| docs/tutorial/12-tooling.md | 2 | 1 | 1 | 1 | 0 |
| docs/tutorial/13-worked-example.md | 3 | 3 | 0 | 6 | 0 |
| docs/tutorial/14-for-the-smalltalk-programmer.md | 0 | 0 | 0 | 5 | 0 |
| examples/README.md | 0 | 0 | 0 | 0 | 0 |
| lib/README.md | 1 | 1 | 0 | 0 | 0 |
| docs/INTEROP.md | 0 | 0 | 0 | 5 | 0 |
| **Total** | **153** | **129** | **24** | **107** | **0** |

"Unchecked" means a `smalltalk` block that states no result (105), or a
`$ protost` command that shows no output (2). Run the tool with `-v` to list
them. Most are grammar fragments, or method bodies that assume context. In
`docs/INTEROP.md` and `docs/tutorial/14-...` no example states a result at all.

## Failures

Diagnoses: **stale** = the doc is stale (the language changed); **wrong** = the
doc example was always wrong; **bug** = it looks like a runtime bug;
**extractor** = the extractor misread it.

**No failure is diagnosed as a runtime bug, and none is an extractor
misreading.** Nothing crashed or hung. In every failure where the program ran
to the end, the value it computed is the value the surrounding prose describes.
What fails is the notation, or the way the example is set up.

### Stale: collections now print their contents (10)

The runtime used to print any collection as `an Array`. With the kernel at
`568e046` it prints the elements. In every case below, the elements printed
are exactly the "facts" the prose lists next to the example.

| Location | Snippet | Expected | Got | Diagnosis |
|---|---|---|---|---|
| docs/tutorial/05-classes-and-methods.md:243 | `inherit.st` → `{ d describe. d legs }` | `an Array` | `#('a dog' 4)` | stale |
| docs/tutorial/08-collections.md:111 | `ordered.st` → `{ oc size. oc first. oc last }` | `an Array` | `#(2 2 3)` | stale |
| docs/tutorial/08-collections.md:171 | `bag.st` → `{ b size. b occurrencesOf: 5 }` | `an Array` | `#(3 2)` | stale |
| docs/tutorial/08-collections.md:196 | `dict.st` → `{ d at: #one. ... }` | `an Array` | `#(1 true 0)` | stale |
| docs/tutorial/08-collections.md:425 | `threshold.st` → `{ highBySelect. highByFold }` | `an Array` | `#(4 4)` | stale |
| docs/tutorial/09-standard-library.md:152 | `stream-demo.st` → `{ first. second. rs atEnd. ws contents size }` | `an Array` | `#(10 20 false 2)` | stale |
| docs/tutorial/09-standard-library.md:180 | `random-demo.st` → `{ r nextInt: 6. r nextInt: 6. r between: 100 and: 200 }` | `an Array` | `#(2 5 114)` | stale (the prose states no values, so these three numbers are not checked against anything) |
| docs/tutorial/09-standard-library.md:218 | `json-demo.st` → `{ name. scores size. back }` | `an Array` | `#('Ada' 3 '[1,2,3]')` | stale |
| docs/tutorial/09-standard-library.md:259 | `time-demo.st` → `{ d asSeconds. longer asSeconds. elapsed >= 0 }` | `an Array` | `#(90 210 true)` | stale |
| docs/tutorial/11-advanced-object-model.md:79 | `mixin.st` → `(a > b) printString , ' and ' , (a < b) printString` | `a Boolean and a Boolean` | `true and false` | stale (Booleans now print as `true`/`false`) |

### Stale: error reports now carry the receiver class and a trace (3)

| Location | Snippet | Expected | Got | Diagnosis |
|---|---|---|---|---|
| docs/tutorial/02-objects-and-messages.md:340 | `$ protost -e '3 fooBar'` | `error: doesNotUnderstand: fooBar` | `error: doesNotUnderstand: fooBar (receiver class: SmallInteger)` / `  at <module> (<expr>:1)` | stale |
| docs/tutorial/02-objects-and-messages.md:349 | `-e '[ 3 fooBar ] on: Error do: [ :e \| e messageText ]'` | `doesNotUnderstand: fooBar` | `doesNotUnderstand: fooBar (receiver class: SmallInteger)` | stale (the added text is part of `messageText` itself) |
| docs/tutorial/07-exceptions.md:62 | `$ protost -e "Error signal: 'disk is full'"` | `error: disk is full` | `error: disk is full` / `  at <module> (<expr>:1)` | stale |

### Stale: other behaviour changes (3)

| Location | Snippet | Expected | Got | Diagnosis |
|---|---|---|---|---|
| docs/tutorial/01-introduction.md:149 | `$ protost -i` session (`3 + 4`, `x := 10`, `x * x`) | banner `protoST 0.1.0-pre — interactive REPL` | banner `protoST 0.3.0 — interactive REPL`; every other line matches | stale (version string) |
| docs/tutorial/12-tooling.md:36 | the same REPL session | `protoST 0.1.0-pre — ...` | `protoST 0.3.0 — ...` | stale (version string) |
| docs/tutorial/10-actors-and-futures.md:379 | `actor-error.st` → `[ f wait ] on: Error do: [ :e \| 'handled: ' , e messageText ]` | `handled: Future rejected: the actor failed` | `handled: the actor failed` | stale. `wait` no longer wraps the text with a `Future rejected:` prefix, which matches "an actor's unhandled exception keeps its class across wait" (158bc5f). The prose at lines 384-386 describes the prefix and is stale too. |

### Doc example was always wrong (8)

| Location | Snippet | Expected | Got | Diagnosis |
|---|---|---|---|---|
| docs/tutorial/04-blocks.md:192 | `$ protost -e '(10 > 3) ifTrue: [ 'bigger' ] ifFalse: [ 'smaller' ]'` | `bigger` | `error: undefined global: bigger` / `  at <module> (<expr>:1)` | wrong: a single-quoted string is nested inside a single-quoted shell argument, so the shell strips the quotes and the runtime receives `[ bigger ]`. The document needs to quote the command with `"..."`. |
| docs/LANGUAGE.md:772 | `lib := Import from: 'counter_lib'. ... c value.` | `20` | `error: module not found: counter_lib` | wrong as shown: `counter_lib` is the test fixture `tests/fixtures/counter_lib.st`, and the text never says so. Run from `tests/fixtures/`, the same code prints `20`, so the semantics are correct. |
| docs/LANGUAGE.md:1028 | `(x > 0) ifTrue: [ 'positive' ]` | `'positive' or nil` | `error: undefined global: x` | wrong: `x` is never defined, and the stated result lists two alternatives, not a value. With `x := 5` the snippet answers `positive`. |
| docs/LANGUAGE.md:1302 | `(#(1 2 3 4) collect: [ :e \| e * e ])` | `an Array 1 4 9 16` | `error: doesNotUnderstand: + (receiver: nil)` at line 1301 | wrong: line 1301 (`#(1 2 3) do: [ :e \| sum := sum + e ]`) uses a `sum` that is never initialised, which aborts the script before this line. Even with `sum := 0` the line would fail as **stale**, because it prints `#(1 4 9 16)`. |
| docs/LANGUAGE.md:1303 | `(#(1 2 3 4) select: [ :e \| e isEven ])` | `an Array 2 4` | same abort (line 1301) | wrong (the same `sum` cause). Once that is fixed, stale: it prints `#(2 4)`. |
| docs/LANGUAGE.md:1304 | `(#(1 2 3 4) inject: 0 into: [ :a :e \| a + e ])` | `10` | same abort (line 1301) | wrong (the same `sum` cause). It prints `10` once `sum` is initialised. |
| docs/LANGUAGE.md:1305 | `(#(3 1 4 1) detect: [ :e \| e > 2 ])` | `3` | same abort (line 1301) | wrong (the same `sum` cause). It prints `3` once `sum` is initialised. |
| docs/LANGUAGE.md:1306 | `(#(1 2) , #(3 4))` | `an Array 1 2 3 4` | same abort (line 1301) | wrong (the same `sum` cause). Once that is fixed, stale: it prints `#(1 2 3 4)`. |

The values quoted for 1302 to 1306 "once `sum` is initialised" come from one
manual run of the same six expressions with `sum := 0` added first. That run
printed `#(6 #(1 4 9 16) #(2 4) 10 3 #(1 2 3 4))`.

### Summary by category

| Category | Count |
|---|---:|
| stale: collection / Boolean printing | 10 |
| stale: richer error reports | 3 |
| stale: REPL banner version, `Future rejected:` prefix | 3 |
| wrong: the `sum` cascade in LANGUAGE.md §9 (one root cause) | 5 |
| wrong: shell quoting, undefined `x`, fixture not on the path | 3 |
| bug | 0 |
| extractor | 0 |

## Unchecked blocks whose prose claims something the runtime contradicts

The tool does not check these, because no block states an output. I ran each
claim by hand against the same binary and kernel, and each one is stale:

| Location | Claim in the document | What the runtime does |
|---|---|---|
| docs/TUTORIAL.md:72 (block at 75) | "top-level temps are NOT supported — this is a parse error" | `\| d \| d := Dictionary new.` runs and prints `a Dictionary()` |
| docs/tutorial/03-variables-and-literals.md:64 (block at 67) | `\| total \|` at top level "fails with a parse error" | runs; `\| total \| total := 0.` prints `0` |
| docs/tutorial/01-introduction.md:140 (prose) | "`-e` ... does **not** accept a `\| temps \|` declaration" | `-e '\| a \| a := 3. a + 1'` prints `4`. `-e '\| f \| f := Future new. f resolve: 99. f wait'` (10-actors-and-futures.md:144) prints `99`. |
| docs/tutorial/05-classes-and-methods.md:426 (block at 429) | assigning a class variable from an instance method "is a compile-time error" | compiles and runs, as expected after e71b8c6 ("class variables may be assigned from instance methods (D19 closed)") |

## Snippet groups applied

Four examples build on an earlier block. I marked each one with
`<!-- snippet-group: NAME -->` directly above the fence. The markers are
invisible when rendered, and no prose was changed. Without them the dependent
examples fail with `undefined global`.

| Document | Group | Marker lines | Why |
|---|---|---|---|
| docs/tutorial/05-classes-and-methods.md | `class-variables` | 402, 418 | `Counter new total printNl. "→ 3"` uses the class declared in the block before it |
| docs/tutorial/05-classes-and-methods.md | `call-form` | 466, 478 | `c incr(2, factor = 3) printNl` uses the call-form `Counter` declared in the block before it |
| docs/tutorial/10-actors-and-futures.md | `atom` | 404, 413 | `total swap: ...` / `"=> 1"` uses the `total` created in the block before it |
| docs/LANGUAGE.md | `atom` | 1624, 1634 | `total value: 1 ifCurrent: 0` uses the `total` created in the block before it |

## Results with the worktree's own `lib/` (kernel mismatch)

These are the same runs with `PROTOST_LIB` set to this branch's `lib/`
(`4080dfd`), which is the command the task specified. Two results differ from
the tables above:

- 9 of the "collections now print their contents" failures do not appear.
  Without `lib/kernel/` the binary falls back to printing `an Array` /
  `a Boolean`, which is what the documents say.
- `docs/tutorial/03-variables-and-literals.md:281`
  (`-e "'hello' at: 1"`, expected `h`) fails with `a Character`.
- `docs/tutorial/09-standard-library.md:218` (`json-demo.st`) fails
  differently: `lib/json.st` at `4080dfd` no longer parses under this binary.
  The error is `264:3 a blank line ends a method body; this indented statement
  would run at top level, outside JSON>>parseNumber:`. `568e046` fixed
  `lib/json.st`.

Neither of these is a runtime bug. Both come from pairing a new binary with an
old kernel. Once this branch is merged into `feature/presentable-0.4.0`, the
tool's default `--lib <root>/lib` is the matching kernel.

## Incidental observation (not a documentation example)

While checking the printing semantics I found that `#y class` answers `String`
and `#y isSymbol` answers `false`. As a result, `#y printString` is `'y'` and
`#(1 #y) printString` is `#(1 'y')`. `lib/kernel/printing.st` expects
`self class == Symbol` to tell a Symbol apart from a String. **This looks like a
runtime bug.** No documentation example exercises it, so it is not counted
above.
