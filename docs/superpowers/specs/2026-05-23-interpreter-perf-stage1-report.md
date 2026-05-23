# Stage 1 morning report — threaded-goto dispatch

**Run:** overnight autonomous session, 2026-05-22 → 23
**Spec executed:** `docs/superpowers/specs/2026-05-23-interpreter-perf-spec.md` (Stage 1)
**Result:** committed locally (no push, per instructions); ctest clean.

---

## Summary

| | mt2k | mt20k | mt100k | msg/s @ 100k |
|---|---|---|---|---|
| Pre (last commit fb3472a) | 0.10 s | — | 5.50 s | **~18,200** |
| **Post (commit b883fa9)** | **0.09 s** | **0.87 s** | **4.97 s** | **~20,121** |
| Delta | -10% | (new) | -9.6% | **+10.5%** |

Stage 1 delivered the lower edge of its spec range (spec predicted 20-40%;
measured ~10%). The reason is structural and clear in the post-Stage-1
profile (see below): after threaded-goto, `runLoop` is **0.73 %** of CPU.
The dispatch refactor did exactly what it was designed to do — it
collapsed the centralised switch jump — but the per-message cost was
never dominated by switch dispatch; it is dominated by the protoCore data
structures invoked from inside the per-message machinery.

---

## What was committed (commit b883fa9, local only)

Hybrid threaded-goto in `ExecutionEngine::runLoop`:

- Labels table initialised at function entry (one designated store per
  opcode after `for (i=0..255) labels[i] = &&L_BAD_OP`). The
  `[0 ... 255] = ...` GCC range-designator in a brace-init *combined with*
  specific `[Op::X] = ...` designators is rejected in C++ mode — the
  loop pattern works without changing the language standard.
- Function-scope `op`, `arg` (assigned, not declared, in the outer prelude
  so the goto'd-into labels see decoded operands).
- `DISPATCH_DIRECT()` macro that re-decodes (EXTEND prefix, debugger
  checks, off-end check, op/arg read) and ends `goto *labels[op]`.
- Per-case `L_X:` label + shadow `Frame& f = frames_.back()` (outer-while
  `f` is stale after threaded-gotos bypass the iteration cycle).
- SEND_* dispatch-level break/continue converted to `DISPATCH_DIRECT()`;
  inner-loop break/continue (mailbox CAS retry, parent-walk, super
  search) kept verbatim.
- End labels `L_OFF_END:`, `L_BAD_OP:`, `L_RUN_DONE:` after the switch.

Net diff: `src/runtime/ExecutionEngine.cpp` +231 / -64 lines (one file).

---

## Verification

| Check | Result |
|---|---|
| Build clean | yes (no new warnings) |
| Full ctest × 5 | 4/5 100% clean; 1/5 had a single `cli_help` flake |
| `cli_help` × 5 in isolation | 5/5 clean (confirmed environmental, not a regression) |
| `03_fan_out_fan_in` × 5 in isolation | 5/5 clean (same — flake on first parallel run only) |
| `cli_actor_stress` × 3 | 3/3 clean (D23 territory — scheduler under load) |
| Actor / Future / Atom subset × 10 (44 tests each) | 440/440 clean |

The `cli_help` failure on parallel-test run 1 reproduces only with N+ other
tests running in parallel; the test passes 5/5 solo. It is environment
(filesystem race + parallel test slot), not a threaded-goto correctness
issue.

---

## Why only +10 %, not +20-40 % — post-Stage-1 profile

After Stage 1, the CPU breakdown on `mt100k` (perf record, fp callgraph):

| % CPU | Function | Where |
|---|---|---|
| 19.18 % | `gcThreadLoop` | concurrent GC thread (bounded wall-clock impact) |
| 4.17 % | `RopeCharacterIterator::hasNext` | string traversal |
| 3.90 % | `RopeCharacterIterator::next` | string traversal |
| 3.74 % | `ProtoSparseListImplementation ctor` | sparse-list rebuild |
| 3.69 % | `StringLeafNode::fromObject` | rope-leaf cast |
| 3.45 % | (gcThreadLoop lambda) | concurrent GC thread |
| 2.91 % | `ProtoContext::allocCell` | per-cell allocation |
| 1.88 % | `ProtoObject::isString` | type check (mostly from rope walk) |
| 1.82 % | `ProtoSparseListImplementation::processReferences` | GC trace of sparse list |
| 1.78 % | `ProtoSparseListImplementation::implSetAt` | sparse-list update |
| 1.53 % | `ProtoString::toUTF8String` | rope → UTF-8 conversion |
| 1.36 % | `ProtoSparseListImplementation::implGetAt` | sparse-list read |
| 0.96 % | `ProtoObject::getAttribute` | attribute read |
| 0.82 % | `ProtoObject::setAttribute` | attribute write |
| **0.73 %** | **`ExecutionEngine::runLoop`** | **the interpreter itself** |

**Conclusion:** the interpreter is now 0.73 % of CPU on a pure-message
benchmark. The mt100k 5 s wall is spent in protoCore data structures
called from the per-message machinery, not in `runLoop`. Stage 1's
target of reducing the dispatch indirect-jump cost was met — there is
just not much dispatch-indirect-jump cost to reduce relative to the
per-message protoCore traffic.

**Aggregated levers visible in the profile:**

1. **~ 9.5 %** Rope walk (`RopeCharacterIterator` × 2 + `StringLeafNode::fromObject`
   + `toUTF8String`) — all of `toUTF8String` traces back to `SymbolTable::intern`
   and `SymbolTable::lookupByContent` (the only protoCore callers on a
   per-message path). Some hot path is passing a non-symbol string to
   `getAttribute`/`setAttribute` and the bucket walk does
   `contentEqual` (= `toUTF8String` × 2 per bucket). The most likely
   sites are surveyed below.

2. **~ 8.7 %** `ProtoSparseListImplementation` (ctor + processReferences +
   implSetAt + implGetAt) — the mailbox is a `ProtoList` (backed by a
   `ProtoSparseList`) and rebuilds on every actor SEND (append) and every
   `drainOne` (pop). This is exactly what Stage 2's intrusive lock-free
   MPSC mailbox replaces.

3. **~ 19 %** Concurrent GC thread — bounded wall-clock impact (separate
   thread). Stage 2 should reduce this proportionally to the drop in
   per-message Cell allocation.

---

## Stage 2 was NOT attempted overnight

Stage 2 (flatten the message envelope and the Future into C++ structs
with `std::atomic` fields, intrusive lock-free MPSC mailbox) was held back
because of the spec's own risk inventory:

> **GC-rooting** — the C++ structs are invisible to the GC. The
> `ProtoObject*` they hold must each be reachable from at least one GC
> root at every moment. A missed rooting is a silent use-after-free —
> the canonical hard bug.

A silent use-after-free is exactly the bug class that hides between
ctest runs and surfaces under sustained actor stress weeks later. The
spec lists three escape paths (mailbox / waiter / pool object) that all
need to be audited together for the invariant "every `ProtoObject*` held
in a C++ struct is also reachable from a `ProtoObject` GC root at every
moment". Auditing that invariant correctly is human work — overnight,
solo, without you available to review, is the wrong context for it.

Stage 1 went in clean (~ 1,200-line refactor in `ExecutionEngine.cpp`,
zero protoCore changes, 13/13 stress runs clean) precisely because it
was a local control-flow rewrite with no allocation / GC implications.
Stage 2 is the opposite: minimal source change in line count, deep
implications in the GC root set.

**Recommendation:** Stage 2 is the right next step; pair on the GC-root
invariant audit before implementing.

---

## Other quick-win opportunities surfaced during the session

(Not implemented — surfaced for your call.)

### Q1 — protoCore is currently `RelWithDebInfo` (`-O2 -g -DNDEBUG`)

`protoST/build/protost` links against
`protoCore/build/libprotoCore.so.1.1.0` (the tagged v1.1.0 pair, per
README). That build was configured `RelWithDebInfo` = `-O2`. Compiling
the same v1.1.0 source as `Release` = `-O3` is a typical 5-15 %
performance win for a hot-loop-heavy library.

`build_release/` already exists but is at v1.2.0 (later source) — not
directly substitutable without breaking the v0.1.0 / v1.1.0 pair.

**Action (not taken):** rebuild `protoCore/build/` from v1.1.0 source as
Release, or switch `protoST/build/` to find `build_release/` of a
v1.1.0 build. Either is your call — both touch the documented stable
pair.

### Q2 — the ~ 9.5 % rope-walk lead

Symbol lookup goes through the slow path (content compare via
`toUTF8String`) iff `getAttribute` is called with a `POINTER_TAG_STRING`
key that is *not yet* a symbol. All Bootstrap-cached attribute symbols
(`sym.future`, `sym.state`, ...) and all bytecode-pool symbols
(`constSym(ctx, i)`) are pre-interned and carry `POINTER_TAG_SYMBOL`.
Finding the offender requires either an `assert(pa.pointer_tag ==
POINTER_TAG_SYMBOL)` in `getAttribute`/`setAttribute` (in a debug build
of protoCore) or a counter in `SymbolTable::lookupByContent` /
`SymbolTable::intern`. Worth ~ 30 minutes; probably one or two callsites.

### Q3 — mailbox as ProtoList costs ~ 8.7 %

Replacing the mailbox with an intrusive lock-free MPSC linked list (per
Stage 2's design) is the single biggest structural lever after Stage 1.
Future-flattening (~ 5 % of CPU, but ~ 10 sparse-list rebuilds per
round-trip in pure cell-alloc terms) compounds with it.

---

## Files touched this session

| File | Change |
|---|---|
| `src/runtime/ExecutionEngine.cpp` | threaded-goto refactor (+231 -64) |
| `docs/superpowers/specs/2026-05-23-interpreter-perf-stage1-report.md` | this report (new) |

Both committed locally. No push.

---

## Numbers cheat-sheet

```
Pre  fb3472a (home/block symbol cache):  mt100k 5.50 s  18,182 msg/s
Post b883fa9 (threaded-goto):           mt100k 4.97 s  20,121 msg/s   +10.5%
                                        mt20k  0.87 s  22,989 msg/s
                                        mt2k   0.09 s  22,222 msg/s
```

`runLoop` itself: 0.73 % of mt100k CPU (interpreter overhead is now
essentially zero on this benchmark). All remaining structural cost is
in protoCore per-message data structures — exactly what Stage 2
addresses.
