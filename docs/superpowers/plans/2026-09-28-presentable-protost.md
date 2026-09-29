# Presentable protoST 0.4.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make protoST presentable and incontestable for the 2026-10-15 talk to classic Smalltalkers: no silent wrong results, no stage hazards, the everyday Smalltalk protocol present, documentation that survives verification, and the talk materials ready — all by Saturday 2026-10-03.

**Architecture:** C++ changes only where the runtime, compiler or GC require them (stage safety, compiler semantics, hashing, LargeInteger, closures). Everything else — the missing protocol — goes into a new **Smalltalk-source kernel** (`lib/kernel/*.st`) loaded at runtime start, so the protocol is written in the language the audience reads. Every fix is pinned by a conformance `.st` test first.

**Tech Stack:** C++20, CMake/CTest, protoCore 2.5.0 (`../protoCore/build_release`), protoST `.st` sources, bash test runners, Python 3 (benchmark harness), `script`/`scriptreplay` (demo recordings), claude.ai Slides artifact (deck).

**Spec:** `docs/superpowers/specs/2026-09-28-presentable-protost-design.md`

## Global Constraints

- Deadline: everything done **Saturday 2026-10-03**; Thursday 2026-10-01 is the re-audit gate.
- Builds: `cmake --build build_release --target protost` — **never `-j`** (DEV12 hangs). One build at a time on the machine.
- Tests: `ctest --test-dir build_release --output-on-failure < /dev/null` — sequential, **always `< /dev/null`** (S18). Full suite green before every commit.
- New conformance test = new file under `tests/conformance/NN-area/`, first line a directive (`"EXPECT: …"`, `"EXPECT-ERROR: …"`); re-run `cmake -S . -B build_release` so the glob picks it up.
- Test first: every new test is run against the current binary and seen to FAIL before the fix.
- Branch: `feature/presentable-0.4.0`; one topic per commit; message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
- Code comments and docs in professional English; talk materials in Spanish.
- A blank line ends a method body (spec W2 decision).
- `//` floors and `\\` is the floor modulo (Smalltalk-80); `rem:`/`quo:` truncate.
- `new` sends `initialize`.
- No marketing wording anywhere; every number cites a dated report.
- Author name in any attribution: "Gustavo Marino".

## Review Focus

1. **Existing programs whose methods contain blank lines** — after W2 they must fail loudly (a statement referencing a method temp at top level → compile error), never silently change meaning. Task 6 adds a test that a method with an internal blank line produces a diagnostic mentioning the blank line.
2. **A collection that contains itself** (`oc add: oc. oc printString`) — must terminate (Pharo prints `...`). Task 14 adds the test.
3. **Closures created inside actor methods and across `wait`** — fresh bindings must hold across message boundaries. Task 9 adds an actor test.
4. **Kernel load cost and failure** — a syntax error in a kernel file must stop startup with the file and line, and startup time must stay under 100 ms. Task 1 adds both checks.
5. **Heap limit hit inside an actor** — OOM inside a worker must end the program with the clean message, not abort. Task 2 adds the actor variant.

## Execution order

Tasks are numbered by theme; several tests use protocol from later tasks, so
execute in this order (each still test-first):

1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 10 → 11 → 13 → 16 → 15 → 17 → 18 → 21 → 22 →
14 → 19 → 20 → 12 → 23 → 24 → 25 → 26 → 27 → 9 → 28 → 29 → 30 → 31 → 32 → 33 →
34 → **35 (Thursday gate)** → 36 → 37 → 38 → 39 → 40.

Target days: Tuesday Tasks 1–11; Wednesday 13–23; Thursday 24–29, 9, 35 and
start 30–33; Friday 30–34, 36; Saturday 37–40.

---

## Phase 0 — Infrastructure (Tuesday morning)

### Task 1: Smalltalk-source kernel loaded at start

**Files:**
- Create: `lib/kernel/00-manifest.txt` (ordered list of kernel files), `lib/kernel/object.st` (initially just `Object >> yourself`)
- Modify: `src/runtime/STRuntime.cpp` (near the lib resolution at ~2122: add `loadKernel()` after bootstrap), `src/runtime/STRuntime.h`, `CMakeLists.txt` (install `lib/kernel/` with the rest of `lib/`)
- Test: `tests/conformance/12-builtins/kernel-yourself.st`, `tests/cli/kernel_errors.sh` (+ registration in `tests/CMakeLists.txt`)

**Interfaces:**
- Produces: every later kernel task adds a file to `lib/kernel/` and a line to `00-manifest.txt`. Kernel files use ordinary protoST file syntax (`X >> sel` methods, blank-line-separated).

- [ ] **Step 1: Write the failing tests**

`tests/conformance/12-builtins/kernel-yourself.st`:
```smalltalk
"EXPECT: 2"
"The kernel (lib/kernel) is loaded before user code: yourself exists."
(OrderedCollection new add: 1; add: 2; yourself) size.
```
`tests/cli/kernel_errors.sh`:
```bash
#!/usr/bin/env bash
# A broken kernel file stops startup with file and line; startup stays fast.
set -u
PROTOST="$1"; SCRATCH="$2"
rm -rf "$SCRATCH" && mkdir -p "$SCRATCH/kernel"
printf 'object.st\n' > "$SCRATCH/kernel/00-manifest.txt"
printf 'Object >> broken\n  ^ ( .\n' > "$SCRATCH/kernel/object.st"
out=$(PROTOST_LIB="$SCRATCH" "$PROTOST" -e '1' 2>&1); rc=$?
[[ $rc -ne 0 && "$out" == *"object.st:2"* ]] || { echo "FAIL: broken kernel not reported: rc=$rc out=$out"; exit 1; }
start=$(date +%s%N); "$PROTOST" -e '1' >/dev/null; end=$(date +%s%N)
ms=$(( (end-start)/1000000 ))
[[ $ms -lt 100 ]] || { echo "FAIL: startup ${ms} ms >= 100"; exit 1; }
echo "PASS"
```
Register in `tests/CMakeLists.txt` next to the other `tests/cli` cases (copy the pattern of an existing `add_test(NAME cli_...)` there), passing `$<TARGET_FILE:protost>` and `${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/kernel`.

- [ ] **Step 2: Run and see them fail**

Run: `cmake -S . -B build_release && ctest --test-dir build_release -R "kernel" --output-on-failure < /dev/null`
Expected: `kernel-yourself` FAIL (`doesNotUnderstand: yourself`); `kernel_errors` FAIL (no kernel mechanism).

- [ ] **Step 3: Implement `loadKernel()`**

Read `STRuntime.cpp:2100-2200` (lib resolution: `$PROTOST_LIB`, then relative to the binary, then the install prefix). Add a method that resolves `<lib>/kernel/00-manifest.txt`, and for each listed file compiles and runs it with the same entry point the CLI uses for a script file (find it in `src/main.cpp`: the function that runs a file). On any compile or runtime error, print `kernel: <file>:<line>: <message>` to stderr and make runtime start fail (non-zero exit). Call it once, right after bootstrap, before user code or the REPL. Content of `lib/kernel/object.st`:
```smalltalk
"Kernel: Object protocol written in protoST itself."

Object >> yourself
  ^ self.
```
`lib/kernel/00-manifest.txt`:
```
object.st
```
Install rule: in `CMakeLists.txt` find `install(DIRECTORY lib/` (or the rule that installs `json.st`), ensure `kernel/` is included.

- [ ] **Step 4: Build and run tests**

Run: `cmake --build build_release --target protost && ctest --test-dir build_release -R "kernel" --output-on-failure < /dev/null`
Expected: both PASS. Also run the full suite: all 856 pass.

- [ ] **Step 5: Measure and commit**

Run `perf stat -r 10 build_release/protost -e 1` before (installed `/usr/bin/protost`) and after; record both in the commit message.
```bash
git add lib/kernel src/runtime/STRuntime.cpp src/runtime/STRuntime.h CMakeLists.txt tests/conformance/12-builtins/kernel-yourself.st tests/cli/kernel_errors.sh tests/CMakeLists.txt
git commit -m "feat(kernel): load a Smalltalk-source kernel at start"
```

---

## Phase 1 — Stage safety (Tuesday)

### Task 2: GC runs by default; out-of-memory is a clean error (S1, S2)

**Files:**
- Modify: `src/runtime/STRuntime.cpp` (where the ProtoSpace is created), `src/main.cpp` (top-level error exit), docs later in Task 33
- Test: `tests/cli/memory_bounded.sh`, `tests/cli/oom_clean.sh`

- [ ] **Step 1: Write the failing tests**

`tests/cli/memory_bounded.sh`:
```bash
#!/usr/bin/env bash
# A 3M-iteration allocating loop must stay under 1 GB RSS with default settings.
set -u
PROTOST="$1"
rss=$( { /usr/bin/time -f '%M' "$PROTOST" -e 's := 0. 1 to: 3000000 do: [:i | s := s + (Array new: 10) size]. s' >/dev/null; } 2>&1 | tail -1 )
[[ "$rss" -lt 1048576 ]] || { echo "FAIL: maxrss ${rss} KB"; exit 1; }
echo "PASS ($rss KB)"
```
`tests/cli/oom_clean.sh`:
```bash
#!/usr/bin/env bash
# Exhausting a small explicit heap ends with a clean message: no abort, no core.
set -u
PROTOST="$1"
ulimit -c 0
for prog in 'a := OrderedCollection new. [true] whileTrue: [a add: (Array new: 100)]' \
            'w := Object new asActor. f := w perform: #yourself. a := OrderedCollection new. [true] whileTrue: [a add: (Array new: 100)]'; do
  out=$(PROTOCORE_HEAP_LIMIT_CELLS=200000 timeout 60 "$PROTOST" -e "$prog" 2>&1); rc=$?
  [[ $rc -ne 0 && $rc -ne 134 && $rc -ne 124 ]] || { echo "FAIL: rc=$rc for: $prog"; exit 1; }
  [[ "$out" == *"out of memory"* ]] || { echo "FAIL: message: $out"; exit 1; }
  [[ "$out" != *"terminate called"* ]] || { echo "FAIL: abort text: $out"; exit 1; }
done
echo PASS
```
Register both like Task 1's CLI test.

- [ ] **Step 2: Run and see them fail** (memory_bounded: multi-GB; oom_clean: rc=134)

- [ ] **Step 3: Implement**

Read `../protoCore/headers/protoCore.h` for `setHeapLimits` and the memory note `reference_protocore_memory_limit` / `reference_protocore_process_sizing_rule` (soft limit triggers collection, hard limit fails). In `STRuntime` after creating the `ProtoSpace`: if `PROTOCORE_HEAP_LIMIT_CELLS` is unset, call `setHeapLimits` with a soft limit sized so the loop above stays bounded (start from 4,000,000 cells soft, 64,000,000 hard; tune by measurement, record the chosen values and their RSS/time in the commit). For S2 find where protoCore reports "heap hard limit … reached" (grep protoCore sources for the message) and how it surfaces (exception type vs `abort`). If it throws, catch it at the top-level run in `main.cpp` and in the actor worker loop, print `error: out of memory (heap limit N cells reached)` and exit 3. If protoCore aborts, register the hard-limit callback protoCore offers (if none, raise the question to the author before changing protoCore — protoCore changes need a full rebuild of all embedders).

- [ ] **Step 4: Build, run both tests, run full suite; measure `benchmarks` int_sum/fib time before/after and put the numbers in the commit message.**

- [ ] **Step 5: Commit** — `fix(runtime): run the GC by default and end cleanly on out-of-memory`

### Task 3: Deep recursion (S3)

**Files:**
- Modify: `src/runtime/ExecutionEngine.h:204` (`kSlotCapacity`), `src/runtime/ExecutionEngine.cpp:~310` (exhaustion check and message)
- Test: `tests/conformance/05-messages/recursion-depth-10000.st`, `tests/conformance/05-messages/recursion-overflow-catchable.st`

- [ ] **Step 1: Failing tests**
```smalltalk
"EXPECT: 10000"
"Recursion of 10 000 frames works (spec S3)."
Object subclass: #R.
R >> down: n
  n = 0 ifTrue: [^ 0].
  ^ 1 + (self down: n - 1).

R new down: 10000.
```
```smalltalk
"EXPECT: caught"
"Unbounded recursion raises a catchable error (recursion-overflow-catchable.st)."
Object subclass: #R.
R >> forever: n
  ^ self forever: n + 1.

[R new forever: 0] on: Error do: [:e | 'caught'].
```
and `tests/conformance/05-messages/recursion-overflow-message.st`:
```smalltalk
"EXPECT-ERROR: stack depth exceeded"
"Uncaught, the error message names stack depth."
Object subclass: #R.
R >> forever: n
  ^ self forever: n + 1.

R new forever: 0.
```

- [ ] **Step 2: Run; expect "engine slot region exhausted".**
- [ ] **Step 3: Implement.** Read how the slot region is allocated (a fixed array of `kSlotCapacity` slots per engine/thread). Make it growable (allocate in chunks and chain, or `std::vector`-style reallocation of the GC-scanned region — check with the GC root registration that the region is re-registered after growth) up to a maximum depth bound (e.g. 1,000,000 slots); on reaching the bound, signal a normal protoST `Error` with messageText `stack depth exceeded (N frames)` through the same path `signal` uses, so `on:do:` catches it at any level including top level.
- [ ] **Step 4: Build; both tests PASS; full suite.**
- [ ] **Step 5: Commit** — `fix(engine): allow deep recursion and make overflow a catchable error`

### Task 4: Ctrl-C (S4)

**Files:** Modify `src/repl/Repl.cpp`, `src/main.cpp`, `src/runtime/ExecutionEngine.cpp` (interrupt check at backward jumps / sends). Test: `tests/cli/sigint.sh`.

- [ ] **Step 1: Failing test**
```bash
#!/usr/bin/env bash
# SIGINT interrupts a running evaluation; script mode exits 130; REPL survives.
set -u
PROTOST="$1"
"$PROTOST" -e '[true] whileTrue: []' & pid=$!; sleep 1; kill -INT $pid; wait $pid; rc=$?
[[ $rc -eq 130 ]] || { echo "FAIL: script rc=$rc"; exit 1; }
out=$( { sleep 1; kill -INT $(pgrep -n -x protost); sleep 1; echo '3 + 4.'; sleep 1; echo ':quit'; } | "$PROTOST" 2>&1 )
[[ "$out" == *"Interrupted"* && "$out" == *"7"* ]] || { echo "FAIL: repl: $out"; exit 1; }
echo PASS
```
(Adjust the REPL input to start the infinite loop first: prepend `echo '[true] whileTrue: [].'` before the first sleep; check the REPL's quit command in `Repl.cpp`.)
- [ ] **Step 2: Run; fails (REPL dies).**
- [ ] **Step 3: Implement:** a `volatile sig_atomic_t` interrupt flag set by a SIGINT handler installed in `main.cpp`; the engine polls it at loop back-edges and sends (find the dispatch loop's existing safepoint/yield check and add the flag check there); on interrupt signal an `Error` subclass `Interrupted` that the REPL catches and reports as `Interrupted`, returning to the prompt; in script mode exit 130.
- [ ] **Step 4–5:** build, test, full suite, commit `feat(cli): Ctrl-C interrupts evaluation instead of killing the REPL`.

### Task 5: CLI robustness (S5)

**Files:** Modify `src/main.cpp` (:57, :108, :162 — the three `ftell` reads). Test: `tests/cli/cli_inputs.sh`.

- [ ] **Step 1: Failing test**
```bash
#!/usr/bin/env bash
set -u
PROTOST="$1"
out=$(printf '3 + 4.\n' | "$PROTOST" /dev/stdin 2>&1); [[ "$out" == "7" ]] || { echo "FAIL pipe: $out"; exit 1; }
out=$("$PROTOST" <(printf '5 * 5.\n') 2>&1); [[ "$out" == "25" ]] || { echo "FAIL procsub: $out"; exit 1; }
out=$("$PROTOST" nonexistent-file.st 2>&1); rc=$?
[[ $rc -ne 0 && "$out" == *"file not found: nonexistent-file.st"* ]] || { echo "FAIL missing: $out"; exit 1; }
echo PASS
```
- [ ] **Step 2–5:** replace `fseek/ftell` sizing with a stream read (`std::ifstream` + `std::istreambuf_iterator`) in one helper `readWholeFile(path, std::string& out)` used by all three sites; in argument parsing, an argument ending in `.st` or naming an existing path is treated as a file, and a `.st` argument that does not exist reports `file not found: <path>` (exit 66). Build, test, full suite, commit `fix(cli): read scripts from pipes and report missing files`.

### Task 6: Unobserved actor errors are reported (S6)

**Files:** Modify `src/primitives/future_prims.cpp` / actor send path in `src/runtime/STRuntime.cpp` (where a Future is rejected). Test: `tests/cli/actor_unobserved_error.sh`.

- [ ] **Step 1: Failing test**
```bash
#!/usr/bin/env bash
set -u
PROTOST="$1"
out=$("$PROTOST" -e 'a := Object new asActor. a fooBarBaz. (a perform: #yourself) wait. 1' 2>&1)
[[ "$out" == *"unhandled error in actor"*"fooBarBaz"* ]] || { echo "FAIL: $out"; exit 1; }
echo PASS
```
- [ ] **Step 2–5:** when a Future is rejected and is garbage/finalised or the program ends without anyone having called `wait`/`thenDo:`/`onError:` on it, print `warning: unhandled error in actor <class>: <messageText>` to stderr. Simplest correct rule: mark a Future "observed" on any consumer call; at rejection time, if unobserved, record it; at program exit (and when the actor mailbox drains) print recorded unobserved rejections. Commit `feat(actors): report errors of unobserved sends`.

---

## Phase 2 — Compiler semantics (Tuesday–Wednesday)

### Task 7: A blank line ends a method body (W2)

**Files:** Modify `src/frontend/Parser.cpp` (method-body parsing), `src/frontend/Lexer.cpp` (emit a blank-line marker token or track line gaps). Tests: `tests/conformance/03-grammar/blank-line-ends-method.st`, `tests/conformance/03-grammar/blank-line-inside-method-diagnostic.st`.

- [ ] **Step 1: Failing tests**
```smalltalk
"EXPECT: 1"
"A blank line ends a method body even without a top-level ^ (spec W2)."
Object subclass: #Counter instanceVariableNames: 'value'.
Counter >> reset
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

c := Counter new.
c reset.
c increment.
c value.
```
```smalltalk
"EXPECT-ERROR: blank line"
"A method temporary used after a blank line is a compile error that explains the rule."
Object subclass: #T.
T >> m
  | x |
  x := 1.

  ^ x.

T new m.
```
- [ ] **Step 2:** run; first prints `Counter` (body swallowed), second may run.
- [ ] **Step 3:** In the lexer, record for each token whether one or more blank lines precede it. In `Parser::parseMethodBody` (find the loop that consumes statements until the next `X >> sel` header or EOF), stop at the first statement token preceded by a blank line. A top-level statement that references an undeclared identifier that was a temp of the method just closed → compile error `'x' is undefined here; note: a blank line ends a method body (file:line)`. Then run ALL `examples/`, `tests/conformance/`, `lib/*.st`, `benchmarks/**/*.st` and docs snippets; fix every file that has a blank line inside a method (remove the blank line or split into methods). Search: `grep -n -B1 -A1 '^$' ` inside method bodies is not mechanical — rely on the full suite plus Task 32's doc runner.
- [ ] **Step 4:** build; both tests PASS; full suite green.
- [ ] **Step 5:** Commit `feat(parser): a blank line ends a method body`.

### Task 8: Instance variables are inherited; undeclared names and ivar shadowing are errors (W1, W3, W4)

**Files:** Modify `src/frontend/Compiler.cpp` (identifier resolution: temps → args → ivars (own + all superclasses) → class vars → globals), `src/primitives/object_prims.cpp` (`subclass:instanceVariableNames:` must record the full ivar list or expose the superclass chain to the compiler). Tests under `tests/conformance/04-object-model/`.

- [ ] **Step 1: Failing tests**

`inherited-ivar-write.st`:
```smalltalk
"EXPECT: 100"
Object subclass: #Account instanceVariableNames: 'balance'.
Account >> balance
  ^ balance.

Account subclass: #Savings instanceVariableNames: 'rate'.
Savings >> setBalance: n
  balance := n.
  ^ self.

(Savings new setBalance: 100) balance.
```
`inherited-ivar-three-levels.st`:
```smalltalk
"EXPECT: 7"
Object subclass: #A instanceVariableNames: 'x'.
A subclass: #B instanceVariableNames: 'y'.
B subclass: #C instanceVariableNames: 'z'.
C >> setX: v
  x := v.
  ^ self.

C >> getX
  ^ x.

(C new setX: 7) getX.
```
`undeclared-in-method-is-error.st`:
```smalltalk
"EXPECT-ERROR: undeclared"
Object subclass: #K instanceVariableNames: 'count'.
K >> bump
  cuont := count + 1.
  ^ self.

K new bump.
```
`temp-shadows-ivar-is-error.st`:
```smalltalk
"EXPECT-ERROR: already defined"
Object subclass: #Foo instanceVariableNames: 'a'.
Foo >> m
  | a |
  a := 'temp'.
  ^ a.

Foo new m.
```
- [ ] **Step 2:** run all four: outputs 0/nil/runs/runs.
- [ ] **Step 3:** Read `Compiler.cpp` identifier resolution (grep `undefined global`, `resolveIdentifier`/`lookupVar`). Make the compiler obtain the complete ivar list of the method's class by walking superclasses (the class objects built by `prim_Object_subclass` — check where ivar names are stored, e.g. an `__ivars__` attribute; store the inherited list or walk parents). Assignment to an identifier that is not temp/arg/ivar/classvar inside a method: compile error `undeclared variable 'cuont' in K>>bump (file:line)`. At top level (scripts) undeclared assignment stays a global definition (current behaviour). Temp or arg with an ivar's name: compile error `'a' is already defined as an instance variable of Foo`. Also re-check ch05's claim that a bare `init.` is an implicit self-send: it is not; the doc is fixed in Task 33.
- [ ] **Step 4–5:** build, four PASS, full suite; commit `fix(compiler): inherited ivars, undeclared names and ivar shadowing`.

### Task 9: Fresh bindings for blocks (W5, D30)

**Files:** Modify `src/frontend/Compiler.cpp` (block/closure variable capture), `src/runtime/ExecutionEngine.cpp` (block creation / activation), possibly `src/primitives/block_prims.cpp`. Tests: remove XFAIL from `tests/conformance/06-blocks/closure-loop-iteration-binding.st` and add:

`06-blocks/block-factory-fresh-activation.st`:
```smalltalk
"EXPECT: 11 21"
mkAdder := [:k | [:x | x + k]].
p := mkAdder value: 10.
q := mkAdder value: 20.
(p value: 1) printString , ' ' , (q value: 1) printString.
```
`06-blocks/counter-factory-independent.st`:
```smalltalk
"EXPECT: 1 1 2"
mk := [| n | n := 0. [n := n + 1]].
c1 := mk value.
c2 := mk value.
a := c1 value. b := c2 value. c := c1 value.
a printString , ' ' , b printString , ' ' , c printString.
```
`06-blocks/collect-blocks-fresh.st`:
```smalltalk
"EXPECT: 100 200 300"
bs := #(1 2 3) collect: [:x | [x * 100]].
((bs at: 1) value) printString , ' ' , ((bs at: 2) value) printString , ' ' , ((bs at: 3) value) printString.
```
`10-actors/closure-in-actor-fresh.st` (Review Focus 3):
```smalltalk
"EXPECT: 0 1 2"
Object subclass: #Maker.
Maker >> blocks
  | out |
  out := OrderedCollection new.
  0 to: 2 do: [:i | out add: [i]].
  ^ out.

bs := (Maker new asActor blocks) wait.
((bs at: 1) value) printString , ' ' , ((bs at: 2) value) printString , ' ' , ((bs at: 3) value) printString.
```
(`SmallInteger >> printString` already answers its digits on the current build.)

- [ ] **Step 2:** run; see 3 3 3 / 21 / etc.
- [ ] **Step 3:** Read how the compiler lowers block-local and outer variables (grep `closure`, `capture`, `cell`, `outer` in `Compiler.cpp` and the engine's block activation). The fix mirrors protoJS's 2026-09-28 capture-scope change (see `../protoJS/ARCHITECTURE.md` §1.3a): each block **activation** must allocate its own storage for its declared temporaries and parameters, and a block closure must capture the *current* storage of the enclosing activation at creation time, not a per-method shared slot. For `to:do:` the loop variable must be a fresh binding per iteration (if the compiler inlines `to:do:`, allocate the iteration variable's cell inside the loop body when the body contains a block that references it). Keep inlined loops without captured variables on the fast path.
- [ ] **Step 4:** all five PASS; full suite; run `benchmarks/run_benchmarks.py` fib/int_sum before/after — report cycles.
- [ ] **Step 5:** Commit `fix(blocks): every block activation and loop iteration gets fresh bindings`.

### Task 10: Number literals (W6)

**Files:** `src/frontend/Lexer.cpp`. Tests in `tests/conformance/02-lexical/`:

```smalltalk
"EXPECT: true"
"Exponent and radix literals (spec W6)."
(1.0e-10 < 0.001) & (1.0e-10 > 0) & (1e3 = 1000) & (2.5e-3 = 0.0025) & (16rFF = 255) & (2r1010 = 10) & (1.5e2 = 150.0).
```
```smalltalk
"EXPECT: 1267650600228229401496703205376"
"Integer literals beyond SmallInteger range are LargeIntegers."
1267650600228229401496703205376.
```
- [ ] Steps: fail (−7.28 / DNU / parse error) → implement: after digits, `r` introduces a radix (2–36) with digits/letters; `e` followed by optional `-` and digits is an exponent (integer mantissa + non-negative exponent stays Integer: `1e3` = 1000; negative exponent or float mantissa → Float); decimal integer literals of any length produce LargeInteger (use the existing LargeInteger construction from `int_prims.cpp`). Keep `x e` (unary send to a variable) working: exponent lexing applies only immediately after a numeric literal with no whitespace. Build, PASS, suite, commit `fix(lexer): exponent, radix and large integer literals`.

### Task 11: `and:`/`or:` with non-blocks and unterminated comments (W13)

**Files:** `src/primitives/bool_prims.cpp`, `src/frontend/Lexer.cpp`. Tests `02-lexical/unterminated-comment.st` (`"EXPECT-ERROR: unterminated comment"` + body `1 + 1. "never closed`), `05-messages/and-with-boolean-argument.st`:
```smalltalk
"EXPECT: false"
true and: false.
```
- [ ] Implement: `and:`/`or:` evaluate the argument with `value` (Booleans answer themselves to `value` — add `Boolean >> value ^ self` in `lib/kernel/boolean.st`), never leak `__bc_ptr__`; lexer reports `unterminated comment starting at file:line`. Commit `fix: and:/or: accept booleans; unterminated comments are errors`.

---

## Phase 3 — Runtime semantics (Wednesday)

### Task 12: Equality and hashing (W7)

**Files:** `src/primitives/collection_prims.cpp` (Set/Dictionary insertion and lookup), `src/primitives/object_prims.cpp` (`~=`, `=`, `hash` defaults), `lib/kernel/object.st`, `lib/kernel/collection.st`. Tests in `09-collections/`:

```smalltalk
"EXPECT: 1 true found"
Object subclass: #P instanceVariableNames: 'k'.
P >> k: v
  k := v.
  ^ self.

P >> k
  ^ k.

P >> = other
  ^ (other isKindOf: P) and: [k = other k].

P >> hash
  ^ k hash.

s := Set new. s add: (P new k: 1); add: (P new k: 1).
d := Dictionary new. d at: (P new k: 2) put: 'found'.
s size printString , ' ' , (s includes: (P new k: 1)) printString , ' ' , (d at: (P new k: 2) ifAbsent: ['missing']).
```
```smalltalk
"EXPECT: true false true"
Object subclass: #Q.
Q >> = other
  ^ true.

(#(1 2 3) = #(1 2 3)) printString , ' ' , (Q new ~= Q new) printString , ' ' , ((OrderedCollection new add: 1; yourself) = (OrderedCollection new add: 1; yourself)) printString.
```
- [ ] Implement: hashed collections call `hash` via a normal send when the key is a user object (fast path for SmallInteger/String/Symbol unchanged), bucket then compare with `=` send; `Object >> ~= other ^ (self = other) not` in kernel; `SequenceableCollection >> =` compares class, size and elements; `Object >> hash` default identity hash; `isKindOf:` from Task 18 (do Task 18 first or inline `other class == P` — `class` from Task 17). Commit `fix(collections): honour user = and hash; element-wise equality`.

### Task 13: `,` and `messageText` (W8, W9)

**Files:** `src/primitives/string_prims.cpp`, `src/primitives/exception_prims.cpp`. Tests:
```smalltalk
"EXPECT-ERROR: doesNotUnderstand"
'x' , 3.
```
(Pharo: `'x' , 3` → error. Choose: a non-collection argument raises `doesNotUnderstand: do:` style or an explicit `Error` 'Cannot concatenate a SmallInteger to a String'; the EXPECT only checks it errors.)
```smalltalk
"EXPECT: true true"
m1 := [Error signal] on: Error do: [:e | e messageText].
m2 := [Error new signal: 'boom'] on: Error do: [:e | e messageText].
(m1 isNil or: [m1 = 'Error']) printString , ' ' , (m2 = 'boom') printString.
```
- [ ] Implement: `,` accepts String/Symbol (and any sequenceable of characters once Task 21 lands); otherwise signal Error `'Cannot append a <Class> to a String'`. `messageText` default: `description` = class name when never set (fix the uninitialised field read that produced 4611686018427388022). Commit `fix: strict string concatenation and default messageText`.

### Task 14: printString, displayString, printOn: (W10)

**Files:** `lib/kernel/printing.st` (new), `src/primitives/object_prims.cpp` / `string_prims.cpp` (remove C++ `printString` fallbacks that answer `a String`, or keep them as `printOn:` primitives), `src/runtime/…` where `printNl` and the auto-print of a script's last value format objects — they must go through `printString`. Tests in `12-builtins/`:
```smalltalk
"EXPECT: 'abc' #sym true $a nil 42 3.5"
('abc' printString) , ' ' , (#sym printString) , ' ' , (true printString) , ' ' , ($a printString) , ' ' , (nil printString) , ' ' , (42 printString) , ' ' , (3.5 printString).
```
```smalltalk
"EXPECT: #(1 $a 'str' #sym #(2 3)) an OrderedCollection(1 2) a Set(5)"
(#(1 $a 'str' #sym #(2 3)) printString) , ' ' , ((OrderedCollection new add: 1; add: 2; yourself) printString) , ' ' , ((Set new add: 5; yourself) printString).
```
```smalltalk
"EXPECT: Point<3,4>"
Object subclass: #MyPoint instanceVariableNames: 'x y'.
MyPoint >> setX: ax y: ay
  x := ax. y := ay.
  ^ self.

MyPoint >> printOn: aStream
  aStream nextPutAll: 'Point<'; print: x; nextPutAll: ','; print: y; nextPutAll: '>'.

(MyPoint new setX: 3 y: 4) printString.
```
```smalltalk
"EXPECT: done"
"A collection that contains itself prints without hanging (Review Focus 2)."
oc := OrderedCollection new. oc add: oc. oc printString. 'done'.
```
```smalltalk
"EXPECT: abc"
'abc' displayString.
```
- [ ] Implement (depends on Task 22 WriteStream): `Object >> printString ^ String new: 32 streamContents...` — simplest: `| s | s := WriteStream on: String new. self printOn: s. ^ s contents`; `Object >> printOn: aStream  aStream nextPutAll: self class article , ' ' , self class name`; per-class `printOn:` for String (quoted, doubled quotes), Symbol (`#`), Character (`$`), Boolean, UndefinedObject, numbers (primitive digits), Array (`#( … )` literal form), other collections (`a Set(…)`), Interval (`(1 to: 5)`); `displayString` like printString but String/Symbol/Character without decoration; recursion guard via a thread-local set of objects being printed (print `...` on re-entry). `printNl` and the auto-print use `printString` (keep `printNl` of a String printing its raw contents as today? Pharo's `printNl` prints printString — decide: follow Pharo; update tests/examples expecting raw output). Commit `feat(kernel): printString/displayString through printOn:`.

### Task 15: LargeInteger arithmetic (W11)

**Files:** `src/primitives/int_prims.cpp`. Tests `12-builtins/largeint-division.st`:
```smalltalk
"EXPECT: 9900 1024 0 true true"
a := 100 factorial / 98 factorial.
b := (2 raisedTo: 100) // (2 raisedTo: 90).
c := (2 raisedTo: 100) \\ (2 raisedTo: 90).
d := ((2 raisedTo: 100) asFloat - 1.2676506002282294e30) abs < 1e15.
e := ((2 raisedTo: 100) sqrt - 1125899906842624) abs < 1.
a printString , ' ' , b printString , ' ' , c printString , ' ' , d printString , ' ' , e printString.
```
- [ ] Implement LargeInteger ÷ LargeInteger (schoolbook long division on the existing digit representation), `//`, `\\` (floor semantics per Task 16), `quo:`, `rem:`, `asFloat` (via top 64 bits + scaling with `ldexp`), `sqrt` (asFloat sqrt), `gcd:` with LargeInteger args. Commit `feat(int): full LargeInteger division, asFloat and sqrt`.

### Task 16: Floor division (W12)

**Files:** `src/primitives/int_prims.cpp`, `lib/kernel/number.st`. Test `12-builtins/floor-division.st`:
```smalltalk
"EXPECT: -4 1 -3 -1 4 -1"
(-7 // 2) printString , ' ' , (-7 \\ 2) printString , ' ' , (-7 quo: 2) printString , ' ' , (-7 rem: 2) printString , ' ' , (7 // 2 + 1) printString , ' ' , (7 \\ -2) printString.
```
- [ ] Implement floor `//` and `\\` (result sign of divisor) for SmallInteger and LargeInteger; `quo:`/`rem:` truncating. Search tests/examples/docs relying on truncation (`grep -rn '//\|\\\\\\\\' tests examples docs lib`) and update them. Commit `fix(int): Smalltalk-80 floor division; add quo: and rem:`.

---

## Phase 4 — Everyday protocol (Wednesday–Thursday; mostly `lib/kernel`)

Each task: write the conformance tests (all listed), see them fail, implement in `lib/kernel/<area>.st` (primitives only where noted), full suite, commit. Kernel files are added to `00-manifest.txt` in this order: `object.st boolean.st number.st character.st string.st symbol.st collection.st sequenceable.st stream.st exception.st transcript.st point.st fraction.st future.st smalltalk.st printing.st`.

### Task 17: `class` as a message

**Files:** `src/frontend/Parser.cpp` (the `class` keyword is currently consumed as class-side method syntax `X class >> sel`; make `class` an ordinary unary selector except in the exact form `Identifier class >> selector` at statement start), `src/primitives/object_prims.cpp` (`class` primitive answering the object's class; `class class` answers a metaclass-like object that prints `<Name> class` and answers `name`, `superclass`, `new`). Tests `04-object-model/class-message.st`:
```smalltalk
"EXPECT: SmallInteger String true OrderedCollection class"
(3 class name) , ' ' , ('a' class name) , ' ' , ((OrderedCollection new class) == OrderedCollection) printString , ' ' , (OrderedCollection class printString).
```
Plus `x class printNl.` at statement start parses (test file whose body is `3 class printNl.` with `"EXPECT: SmallInteger"`).

### Task 18: Object protocol

**Files:** `lib/kernel/object.st`; primitives only for `perform:`/`respondsTo:` if no reflective primitive exists (grep `respondsTo`, `perform` in `object_prims.cpp`). Tests `12-builtins/object-protocol.st`:
```smalltalk
"EXPECT: true false true true 7 7 caught copy-ok nil-ok"
Object subclass: #W instanceVariableNames: 'v'.
W >> v: x
  v := x.
  ^ self.

W >> v
  ^ v.

W >> abstract
  ^ self subclassResponsibility.

w := W new v: 5.
r1 := (3 isKindOf: Number) printString.
r2 := (3 isMemberOf: Number) printString.
r3 := (w respondsTo: #v) printString.
r4 := (w respondsTo: #abstract) printString.
r5 := (3 perform: #+ with: 4) printString.
r6 := (3 perform: #+ withArguments: #(4)) printString.
r7 := [w abstract] on: Error do: [:e | 'caught'].
c := w copy. c v: 9.
r8 := (w v = 5 and: [c v = 9]) ifTrue: ['copy-ok'] ifFalse: ['copy-bad'].
r9 := (nil isNil and: [3 notNil]) ifTrue: ['nil-ok'] ifFalse: ['nil-bad'].
r1 , ' ' , r2 , ' ' , r3 , ' ' , r4 , ' ' , r5 , ' ' , r6 , ' ' , r7 , ' ' , r8 , ' ' , r9.
```
and `12-builtins/object-error.st`:
```smalltalk
"EXPECT: bad input"
[Object new error: 'bad input'] on: Error do: [:e | e messageText].
```
Implement: `isKindOf:` (walk `class`/`superclass`), `isMemberOf:`, `respondsTo:` (method lookup primitive), `perform:`/`perform:with:`/`perform:with:with:`/`perform:withArguments:`, `error:`, `subclassResponsibility`, `shouldNotImplement`, `copy` (shallow: new instance with ivars copied — primitive `shallowCopy` if absent), `deepCopy` OUT, `hash`, `asString` (= displayString), `isNil`/`notNil`/`ifNil:`/`ifNotNil:`/`ifNil:ifNotNil:`, `->`, `isString`/`isNumber`/`isSymbol`/`isCollection`/`isBlock`, `name` on classes, `superclass`.

### Task 19: Numbers and blocks protocol

**Files:** `lib/kernel/number.st`, `lib/kernel/block.st`. Test `12-builtins/number-block-protocol.st`:
```smalltalk
"EXPECT: 3 FF 1010 true 2 3 6 true"
n := 0. 3 timesRepeat: [n := n + 1].
a := n printString.
b := 255 printString: 16.
c := 10 radix: 2.
d := (3 between: 1 and: 5) printString.
e := [:x :y | x + y] numArgs printString.
f := ([:x :y | x + y] valueWithArguments: #(1 2)) printString.
g := (#(1 2 3) inject: 0 into: [:s :x | s + x]) printString.
h := (7 isPrime) printString.
a , ' ' , b , ' ' , c , ' ' , d , ' ' , e , ' ' , f , ' ' , g , ' ' , h.
```
Implement `timesRepeat:`, `printString:` (base), `radix:`, `printPaddedWith:to:`, `between:and:`, `max:`/`min:` (if absent), `isPrime`, `even`/`odd`, `sign`, `squared`, `isZero`, `to:collect:`; blocks `numArgs`, `valueWithArguments:`, `whileFalse:`, `repeat` (if absent), `on:do:on:do:` exists.

### Task 20: Collections protocol and sorting

**Files:** `lib/kernel/collection.st`, `lib/kernel/sequenceable.st`; a sort primitive (merge sort in C++ over an Array with a block comparator) in `collection_prims.cpp` if a Smalltalk merge sort is too slow (measure 100 000 elements < 1 s). Tests `09-collections/sequenceable-protocol.st`:
```smalltalk
"EXPECT: 1 3 true 2 #(3 2 1) #(2 3) 3 6 #(1 2 3) #(3 2 1) #(1 1 2 3) 1-a2-b"
a := #(1 2 3).
r := OrderedCollection new.
a withIndexDo: [:e :i | r add: i].
s := WriteStream on: String new.
#($a $b) withIndexDo: [:c :i | s print: i; nextPut: $-; nextPut: c] .
t1 := a first printString , ' ' , a last printString , ' ' , (a includes: 2) printString , ' ' , (a indexOf: 2) printString.
t2 := a reversed printString , ' ' , (a copyFrom: 2 to: 3) printString , ' ' , a max printString , ' ' , (a inject: 0 into: [:x :y | x + y]) printString.
t3 := #(3 1 2) asSortedCollection asArray printString , ' ' , (#(1 3 2) asSortedCollection: [:x :y | x > y]) asArray printString , ' ' , (#(3 1 2 1) asOrderedCollection sort; yourself) asArray printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , s contents.
```
`09-collections/dictionary-protocol.st`:
```smalltalk
"EXPECT: 2 #b 3 true"
d := Dictionary new.
d add: #a -> 1; add: #b -> 2.
d at: #c put: 3.
(d at: #b) printString , ' ' , (d keyAtValue: 2) printString , ' ' , d size printString , ' ' , (d includesKey: #a) printString.
```
Implement: `first`, `last`, `allButFirst`, `allButLast`, `includes:`, `indexOf:`, `reverse`/`reversed`, `copyFrom:to:`, `copyWith:`, `withIndexDo:`, `doWithIndex:`, `keysAndValuesDo:` on sequences, `max`, `min`, `sum`, `detectMax:`, `isEmpty`/`notEmpty`/`isEmptyOrNil`, `anyOne`, `asArray`, `asOrderedCollection`, `asSet`, `asBag` (OUT unless trivial), `asSortedCollection`, `asSortedCollection:`, `sort`, `sort:`, `sorted`, `sorted:`, `SortedCollection` (add: keeps order), `addAll:`, `removeAll:`, `remove:`/`remove:ifAbsent:`, `addFirst:`, `removeLast`, `Dictionary add:`, `keyAtValue:`, `keys`, `values`, `associationsDo:`, `removeKey:`, `includesKey:`, `collect:` on Dictionary (answers a Bag in Pharo — answer an OrderedCollection and document).

### Task 21: Strings, Symbols, Characters

**Files:** `lib/kernel/string.st`, `lib/kernel/symbol.st`, `lib/kernel/character.st`; primitives for `at:put:` on a String copy and character access if missing. Tests `12-builtins/string-protocol.st`:
```smalltalk
"EXPECT: cba ABC 2 hellO world true 42 3.5 #abc true 3 bc true true"
s := 'abc'.
r := OrderedCollection new.
s do: [:c | r add: c].
t1 := s reversed , ' ' , s asUppercase , ' ' , (s indexOf: $b) printString , ' ' , ('hello' copyReplaceAll: 'o' with: 'O') , ' ' , 'world'.
t2 := ('abc' < 'abd') printString , ' ' , '42' asNumber printString , ' ' , '3.5' asNumber printString , ' ' , 'abc' asSymbol printString.
t3 := (s isEmpty not) printString , ' ' , r size printString , ' ' , (s copyFrom: 2 to: 3) , ' ' , (s includesSubstring: 'bc') printString , ' ' , ($a isVowel) printString.
t1 , ' ' , t2 , ' ' , t3.
```
Implement String: `do:`, `collect:`, `select:`, `detect:ifNone:`, `reverse`/`reversed`, `asUppercase`, `asLowercase`, `indexOf:`, `occurrencesOf:`, `copyReplaceAll:with:`, `copyFrom:to:`, `copyWith:`, `includesSubstring:`, `beginsWith:`/`endsWith:`, `substrings`/`substrings:`, `lines`, `trimBoth`, `<`/`<=`/`>`/`>=`, `asSymbol`, `asNumber`, `asInteger` (parse decimal digits; `'42' asInteger` = 42 — fixes the code-point bug), `isEmpty`/`notEmpty`, `first`/`last`, `at:put:` (on a copy; literals immutable — error message says so), `,` with any string-like, `join:`, `format:` OUT. Symbol: `asString`, `size`, `value:` OUT. Character class: `value`, `asCharacter` on Integer, `isVowel`, `isLetter`, `isDigit`, `isUppercase`, `asUppercase`, `asLowercase`, `asString`, `printString` `$a`. Globals `Character`, `Integer`, `UndefinedObject` bound.

### Task 22: Streams

**Files:** `lib/stream.st` (exists — read it), `lib/kernel/stream.st` (move/extend so `WriteStream`/`ReadStream` need no import). Test `12-builtins/stream-protocol.st`:
```smalltalk
"EXPECT: a-b-c 1 2 3 true"
w := WriteStream on: String new.
#('a' 'b' 'c') do: [:e | w nextPutAll: e] separatedBy: [w nextPut: $-].
r := ReadStream on: #(1 2 3).
out := OrderedCollection new.
[r atEnd] whileFalse: [out add: r next].
w contents , ' ' , (out at: 1) printString , ' ' , (out at: 2) printString , ' ' , (out at: 3) printString , ' ' , r atEnd printString.
```
Implement `WriteStream on:`/`with:`, `nextPutAll:`, `nextPut:`, `print:`, `display:`, `tab`, `space`, `cr`, `contents`; `ReadStream on:`, `next`, `peek`, `atEnd`, `skip:`, `upTo:`, `upToEnd`; `String class >> new: n streamContents: [:s | …]` and `String streamContents:`.

### Task 23: Exceptions and doesNotUnderstand:

**Files:** `lib/kernel/exception.st`, `src/primitives/exception_prims.cpp` (ZeroDivide class raised by division; MNU carries message/receiver), `src/runtime/ExecutionEngine.cpp` (DNU dispatch: if the receiver's class defines `doesNotUnderstand:`, send it with a `Message` object). Tests in `08-exceptions/`:
```smalltalk
"EXPECT: zd fooBar 3 set 42 Error"
r1 := [1 / 0] on: ZeroDivide do: [:e | 'zd'].
r2 := [3 fooBar] on: MessageNotUnderstood do: [:e | e message selector].
r3 := [3 fooBar] on: MessageNotUnderstood do: [:e | e receiver].
r4 := [1 / 0] on: ZeroDivide, MessageNotUnderstood do: [:e | 'set'].
r5 := [Error signal: 'x'. 1] on: Error do: [:e | e return: 42].
r6 := [Error new signal] on: Error do: [:e | e description].
r1 , ' ' , r2 , ' ' , r3 printString , ' ' , r4 , ' ' , r5 printString , ' ' , r6.
```
```smalltalk
"EXPECT: forwarded: size"
Object subclass: #Proxy instanceVariableNames: 'target'.
Proxy >> target: t
  target := t.
  ^ self.

Proxy >> doesNotUnderstand: aMessage
  ^ 'forwarded: ' , aMessage selector.

(Proxy new target: 'abc') size.
```
Implement `ZeroDivide`, `MessageNotUnderstood` (subclass of Error with `message`, `receiver`, resumable per Pharo), `Message` (`selector`, `arguments`), `ExceptionSet` via `Exception class >> ,`, `ex return`, `ex description`, `ex signalerContext` OUT, `Warning` resumable (exists), `Error class >> signal:` (exists), `ensure:`/`ifCurtailed:` unchanged. `on: ZeroDivide` must also catch integer and float division by zero.

### Task 24: Transcript and Smalltalk

**Files:** `lib/kernel/transcript.st`, `lib/kernel/smalltalk.st`, primitive for writing to stdout without newline if absent (`printString:`/`displayNl` exist? grep `displayNl`, `stdout` in primitives). Tests: remove XFAIL from `tests/conformance/.../no-transcript.st` (rename to `transcript.st`):
```smalltalk
"EXPECT: hello 42"
Transcript show: 'hello'; show: ' '; print: 42; cr.
```
```smalltalk
"EXPECT: 7"
Smalltalk at: #Answer put: 7.
Smalltalk at: #Answer.
```
Transcript writes to stdout immediately, flushing on `cr`/`flush`; `show:` accepts any object (displayString), `print:`, `display:`, `tab`, `space`, `cr`, `nl`. `Smalltalk at:`/`at:put:`/`at:ifAbsent:`/`includesKey:` on the global table.

### Task 25: Point and Association

**Files:** `lib/kernel/point.st`, `lib/kernel/object.st` (`->`). Test `12-builtins/point-association.st`:
```smalltalk
"EXPECT: 3@4 4@6 5.0 #a->1 #a 1"
p := 3 @ 4.
q := p + (1 @ 2).
d := (0 @ 0) dist: p.
a := #a -> 1.
p printString , ' ' , q printString , ' ' , d printString , ' ' , a printString , ' ' , a key printString , ' ' , a value printString.
```
Requires the lexer to accept `@` as a binary selector (check `3 @ 4` today: DNU, so it lexes). Implement `Number >> @`, `Point` x/y/`+ - * /`/`=`/`hash`/`dist:`/`printOn:`; `Association` key/value/printOn:/`=`.

### Task 26: Fraction

**Files:** `lib/kernel/fraction.st`, `src/primitives/int_prims.cpp` (`/` on integers answers a Fraction when inexact — implement by making Integer `/` a kernel method that calls the existing integer-division primitive only when exact, else `Fraction numerator:denominator:`). Test `12-builtins/fraction.st`:
```smalltalk
"EXPECT: (1/2) (3/4) 1 0.75 true (1/3) (7/6) true"
a := (1/3) + (1/6).
b := 3/4.
c := (3/4) * 4/3.
d := (3/4) asFloat.
e := (1/2) < (2/3).
f := (1/3) printString.
g := (1/2) + (2/3).
h := ((1/3) + 0.5 - 0.8333333333333334) abs < 1e-12.
a printString , ' ' , b printString , ' ' , c printString , ' ' , d printString , ' ' , e printString , ' ' , f , ' ' , g printString , ' ' , h printString.
```
(Printing follows Pharo: a Fraction prints in parentheses, `(1/2)`.) Implement numerator/denominator with gcd reduction, sign on numerator, `+ - * /` with Integer/Fraction/Float, comparisons, `=`, `hash`, `negated`, `reciprocal`, `asFloat`, `truncated`, `rounded`, `printOn:`, `isFraction`. Update every test/example/doc that relied on `3/4 = 0` (grep `/ ` in tests and docs; STATUS/LANGUAGE §12 statement changes).

### Task 27: `new` sends `initialize` (D4)

**Files:** `src/primitives/object_prims.cpp` (`new` primitive) or kernel `Object class >> new ^ self basicNew initialize` with `basicNew` the raw allocator; `Object >> initialize ^ self`. Test `04-object-model/new-sends-initialize.st`:
```smalltalk
"EXPECT: 0 1"
Object subclass: #C instanceVariableNames: 'n'.
C >> initialize
  n := 0.

C >> bump
  n := n + 1.
  ^ n.

c := C new.
(c bump - 1) printString , ' ' , (C basicNew initialize; yourself) bump printString.
```
Then fix every example/test/doc that calls `initialize` explicitly after `new` (it would now run twice — harmless in most, but counters double; grep `new initialize`, `c initialize`). Remove D4 from STATUS deviations; update examples/basics/01 comment.

### Task 28: Runtime error locations and short stack trace

**Files:** `src/runtime/ExecutionEngine.cpp` (the uncaught-error printer), `src/frontend/Compiler.cpp` (emit line table per method if absent — check `--dump-ast` / debugger `BreakpointTable` which already maps lines). Test `tests/cli/error_location.sh`:
```bash
#!/usr/bin/env bash
set -u
PROTOST="$1"; F=$(mktemp --suffix=.st)
printf 'Object subclass: #A.\nA >> outer\n  ^ self inner.\n\nA >> inner\n  ^ nil qux.\n\nA new outer.\n' > "$F"
out=$("$PROTOST" "$F" 2>&1)
[[ "$out" == *"doesNotUnderstand: qux"* && "$out" == *"A>>inner"*":6"* && "$out" == *"A>>outer"*":3"* ]] || { echo "FAIL: $out"; exit 1; }
echo PASS
```
Implement: when an error is uncaught, print the message, then one line per active method frame `  at A>>inner (file.st:6)` innermost first, up to 20 frames.

### Task 29: Future combinators

**Files:** `lib/kernel/future.st` (or `future_prims.cpp` if Futures lack a callback hook usable from Smalltalk — `thenDo:` exists). Test `10-actors/future-combinators.st`:
```smalltalk
"EXPECT: 6 6 2"
Object subclass: #Calc.
Calc >> twice: n
  ^ n * 2.

a := Calc new asActor.
f1 := a twice: 1.
f2 := a twice: 2.
all := Future whenAll: (Array with: f1 with: f2).
both := (f1 & f2) wait.
any := (Future whenAny: (Array with: f1 with: f2)) wait.
((all wait) inject: 0 into: [:s :x | s + x]) printString , ' ' , (both inject: 0 into: [:s :x | s + x]) printString , ' ' , ((any = 2) | (any = 4) ifTrue: ['2'] ifFalse: ['bad']).
```
(`f1` resolves to 2 and `f2` to 4, so both sums are 6, and `any` is 2 or 4.) Implement `whenAll:` (Future resolved with an Array of results in order, rejected on first rejection), `whenAny:` (first resolved), `&` (= whenAll: of two), `|` (= whenAny: of two).

---

## Phase 5 — Evidence (Thursday–Friday)

### Task 30: Benchmarks verify their results; honest re-measurement

**Files:** every `benchmarks/**/*.st` (end with a check that prints `VERIFIED <value>` or signals an error), `benchmarks/run_benchmarks.py` (parse stdout, fail the run on a missing/incorrect `VERIFIED`, default binary `build_release/protost`, pass `BENCH_N` to CPython twins so both sides run the same N, record CPU model, load average, protoST git hash, CPython version), `benchmarks/README.md`, new `benchmarks/reports/2026-10-0X-release-0.4.0.md`.
- [ ] Write a harness self-test: a fake benchmark that prints a wrong `VERIFIED` value → harness exit ≠ 0 (`benchmarks/tests/test_harness.py`, run with `python3 -m pytest` or plain asserts; register in CTest as `bench_harness_selftest`).
- [ ] Run the suite on an idle machine (check `uptime` load < 1), 5 repetitions, report median and spread; include the parallel scaling (`saturation_big` w=1..6) and `pump_twin` w=1 vs default.
- [ ] README: replace every number with the new report's, cite it, state the machine.
- [ ] Commit `bench: self-verifying benchmarks and a 0.4.0 report`.

### Task 31: Interop claim verified or narrowed

- [ ] Read README:250-270 and `docs/INTEROP.md`; find the tests (`tests/unit/test_t5a_interop.cpp`) and how protoScala publishes. Try an end-to-end program where protoST uses an object published by a real protoScala (and, if the mechanism allows, protoJS/protoPython) runtime in one process; if it works, add it as a CLI test + example `examples/interop/` and a talk demo candidate. If it does not, rewrite the claim to exactly what is tested ("the protocol is implemented and tested with a simulated provider and with protoScala as publisher") — no broader wording. Commit accordingly.

### Task 32: Documentation snippets run in CTest

**Files:** Create `tests/docs/run_doc_snippets.py` (extract every fenced block tagged ```` ```smalltalk ```` followed by an output block or an `"=> value"` comment from `README.md`, `docs/LANGUAGE.md`, `docs/TUTORIAL.md`, `docs/tutorial/*.md`, `examples/README.md`, `lib/README.md`; also every `$ protost -e '…'` line followed by its output line; run each with the built binary; compare), register one CTest case per document (`docs/<name>`). Blocks intentionally not runnable are tagged ```` ```smalltalk no-run ```` with a reason nearby.
- [ ] Run it: expect many failures today — that list is the input of Task 33.
- [ ] Commit the runner (failing cases registered but marked `WILL_FAIL` only for the documents Task 33 has not yet fixed; Task 33 removes each `WILL_FAIL` as it fixes a document).

### Task 33: Documentation overhaul

**Files:** `README.md`, `DESIGN.md`, `KNOWN_ISSUES.md`, `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/TUTORIAL.md`, `docs/tutorial/*.md` (esp. 01 and 14), `docs/ROADMAP.md`, `docs/INTEROP.md`, `docs/INSTALLATION.md`, `docs/CONFORMANCE.md`, `examples/README.md`, `lib/README.md`, `CHANGELOG.md`, package description in `CMakeLists.txt`.
- [ ] Positioning sentence, used verbatim in README first paragraph, tutorial ch01, LANGUAGE §1, ROADMAP, INSTALLATION and the package description:
  > protoST is a runtime for Smalltalk-syntax programs built on protoCore: programs are plain files, objects are actors that run on native threads, and the runtime can live next to other protoCore runtimes in one process. It is a demonstrator of protoCore and a base for digital twins, not an implementation of Smalltalk-80 and not a replacement for an image-based environment.
- [ ] Tutorial ch14 becomes the single deviation catalogue: generated from STATUS deviations still open after Phases 1–4; each entry: what differs, why, what to do instead. Expected remaining entries: file-based (no image, no IDE/browser), metaclass tower limited to `class`/`class class`, no `thisContext`, no `become:`, blank line ends a method, top-level temps, S19 status. Everything fixed in Phases 1–4 removed from every "limitations" list.
- [ ] Fix every audit item listed in spec §3.4 (Future combinators now real; `yourself`, `error:`, Transcript examples now valid; implicit self-send example corrected; class-variable staleness; shell quoting in tutorial/04:192; top-level temps example in tutorial/10:144; open-bug counts; runtime counts — protoJS, protoPython, protoClojure, protoScala, protoST: say "the other protoCore runtimes" and list them once in README; test counts from `ctest -N` at release; INSTALLATION soname 3, protoCore ≥ 2.2 / package ≥ 2.1.0 as actually built, SHLIBDEPS enabled; STATUS lock claim precise; ch14 K1/D2 update; examples index (pump_twin path, both 05 files); arity claim; error-text samples; D32 decided by the author or reworded as a documented decision without "agent, pending review"; README's Pharo/Vats comparison replaced by a precise statement of protoST's own model without characterising other systems).
- [ ] Every doc passes Task 32's runner; remove all `WILL_FAIL`.
- [ ] Commit per document group.

### Task 34: S18, housekeeping and release 0.4.0

- [ ] S18: `tests/unit/test_debugger.cpp:19` — give the debugger test its own input stream (e.g. an `std::istringstream`) so `ctest` without `< /dev/null` does not hang; test by running that case without the redirect under `timeout 60`.
- [ ] Delete stale local branches (`git branch -d` the four merged ones); remove the broken `build/` directory or rebuild it so `./build/protost` works; confirm no document tells users to run a binary path that does not exist.
- [ ] Version 0.4.0 in `CMakeLists.txt` (`project(... VERSION`), `--version`, CHANGELOG `[0.4.0] - 2026-10-03` section summarising Phases 1–5 with numbers.
- [ ] Full CTest; tag `v0.4.0` (after the author's merge); `.deb` via `cmake --build build_pkg_sep2026 --target protost && (cd build_pkg_sep2026 && cpack -G DEB)`; verify the packaged binary runs every example; give the author the `sudo dpkg -i` command.

---

## Phase 6 — Gate (Thursday)

### Task 35: Independent adversarial re-audit

- [ ] Dispatch three fresh agents with the same three briefs used on 2026-09-28 (claims vs docs; adversarial Smalltalk semantics; tests/CI/benchmarks/demos), against the branch head binary.
- [ ] Every silent wrong result, crash, hang or contradicted claim found → a new task appended here, fixed before release. Any §3.3 item still missing is reported to the author with the reason.
- [ ] Record the audit results in `docs/superpowers/specs/2026-10-01-reaudit.md`.

---

## Phase 7 — The talk (Friday–Saturday)

All materials in `docs/talks/2026-10-15-fas/` (Spanish).

### Task 36: Demo scripts

**Files:** `docs/talks/2026-10-15-fas/demos/01-codigo-conocido.st` (classes, inheritance, blocks, non-local return, exceptions with retry/resume, collections, printString — "the code you know"), `02-actores-paralelismo.st` (the same computation serial vs actors; prints measured ms for 1 and N workers and the result verified), `03-gemelo-digital.st` (pump twin with per-cycle readings printed — extend `examples/pump_twin.st` into a narrated version), optional `04-interop.st` (only if Task 31 verified it), `run_demo.sh` (sets a heap limit, the worker count, runs a demo and checks its expected output), `check_all.sh` (runs every demo 20× and fails on any deviation).
- [ ] Each demo prints verified results (not timings alone) and runs < 5 s.
- [ ] `check_all.sh` passes 20/20 for every demo on the release binary.
- [ ] Commit.

### Task 37: Recorded fallbacks

- [ ] For each demo: `script -q --timing=<demo>.timing <demo>.typescript -c "./run_demo.sh <demo>"`; replay check with `scriptreplay --timing=<demo>.timing <demo>.typescript`. Store under `docs/talks/2026-10-15-fas/recordings/`. Commit.

### Task 38: The deck

- [ ] `Artifact` quickstart with `intent: "slides"`; create the deck from the Slides type; content per spec §3.5: 30-minute core (≈14 slides) + modules (protoCore internals; digital twin walkthrough; interop if verified; benchmarks and methodology; roadmap and open questions). Every number cites the Task 30 report; no superlatives. Speaker notes on every slide.
- [ ] Share the link with the author; export PDF into `docs/talks/2026-10-15-fas/`.

### Task 39: Speaker script and hard questions

**Files:** `docs/talks/2026-10-15-fas/guion.md` (minute-by-minute core and modules, where each demo runs, fallback cue), `preguntas-dificiles.md` (honest, sourced answers: ¿Por qué no Pharo? ¿Dónde está la imagen / el IDE? ¿Rendimiento frente a Pharo / CPython? ¿GC? ¿Por qué prototipos? ¿Quién lo usa? ¿Qué falta? ¿Qué pasa con S19? ¿Licencia? ¿Cómo contribuyo?), `checklist-dia-de-la-charla.md` (install from .deb on the presenting laptop, `protost --version`, run `check_all.sh`, recordings open, terminal font size, offline copy of the deck).
- [ ] Commit; hand off to the author.

### Task 40: Release and handoff

- [ ] Merge `feature/presentable-0.4.0` to `main` (`--no-ff`, `merge(...)` message per repo convention), push, tag `v0.4.0`, rebuild `.deb` from `main`, verify, hand the author: the `sudo dpkg -i` command, the deck link, the talk folder, and the re-audit report.
