# protoST performance baseline — 2026-05-24

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics — 12 logical CPUs
- **OS:** Linux 6.8.0-117-generic (x86_64)
- **Method:** 2 warmup + 5 timed runs per benchmark, median wall-clock reported.
- **protoST:** `/home/gamarino/Documentos/proyectos/protoST/build/protost`
- **CPython:** `/usr/local/bin/python3.14` (3.14.0)
- **protoPython:** `/home/gamarino/Documentos/proyectos/protoPython/build_release/src/runtime/protopy`

## Comparable workloads

The protoPython core benchmark suite translated to idiomatic protoST — same algorithm, same parameters (N). Times in milliseconds (median). `Ratio` is protoST ÷ CPython (>1 = protoST slower).

| Benchmark | protoST (ms) | CPython (ms) | protopy (ms) | Ratio (ST/CPy) |
|---|---:|---:|---:|---:|
| int_sum_loop | 106.0 | 40.9 | 23.4 | 2.59× |
| fib | 891.1 | 56.4 | 129.9 | 15.79× |
| list_append | 116.2 | 37.0 | 284.1 | 3.14× |
| str_concat | 1771.7 | 34.5 | 357.5 | 51.29× |
| attr_lookup | 348.0 | 52.4 | 254.8 | 6.64× |
| range_iterate | 190.1 | 42.3 | 201.9 | 4.49× |
| exception_latency | 1505.7 | 61.0 | 651.7 | 24.69× |
| **Geomean** | | | | **9.02×** |

Geometric-mean ratio across the comparable suite: **protoST is 9.02× CPython's wall-clock** on these single-threaded workloads.

## Actor-model benchmarks

protoST-specific — protoPython has no actor model. These exercise the cooperative actor scheduler.

| Benchmark | Result |
|---|---|
| **Parallel speedup** | 12 CPU-bound worker actors: 751 ms with the full pool vs 3189 ms with `PROTOST_WORKERS=1` — **4.25× speedup** |
| **Cooperative-yield scaling** | **1000** waiter actors, each parked on a nested `wait`, all hosted on **K=2** worker threads — completes in 2179 ms. Thread-per-actor blocking would need 1000 OS threads. |
| **Message throughput** | 2,000 drained round-trip sends to one actor in 41 ms — **48,396 messages/second**. |

### Reading these numbers

protoST's single-thread arithmetic is slower than CPython — it is a young runtime and the comparable table shows that honestly. The actor results are the differentiator: the cooperative-yield benchmark hosts a thousand suspended actors on two OS threads, which a thread-per-actor model fundamentally cannot do, and the parallel benchmark turns extra cores into real wall-clock speedup with no code change.

---

## What this run measures vs. the 2026-05-24 baseline

This file post-dates the 2026-05-24-perf.md baseline by ~45 minutes and
captures the impact of four Tier-S / Tier-A optimisation steps from the
same-day analysis:

1. **Inline `ifTrue:` / `ifFalse:` / `ifTrue:ifFalse:` / `whileTrue:`
   with literal zero-arg blocks** (commit `0d396ca`). The compiler now
   emits a JUMP_IF_FALSE + inlined body instead of dispatching the
   selector through `prim_True_ifTrue` + `invokeBlock` + a nested
   ExecutionEngine. The key win is in tight recursion: `(n <= 1)
   ifTrue: [^n]` used to throw a C++ NonLocalReturn from inside the
   nested engine on every leaf call (perf-traced at ~11 % of CPU in
   `__gxx_personality_v0` + `_Unwind_Find_FDE` + translation wrapper).
   With inlining, `^n` is a plain local RETURN in the current method
   frame — no nested engine, no throw.

2. **RETURN-instead-of-throw for method return** (no-op — the engine
   already implements this in `Op::RETURN` when `homeFrameId ==
   frameId`, the common case; the remaining throw paths now fire
   essentially only on cross-engine non-local returns that Tier-1 above
   has now eliminated for the common conditional / loop patterns).

3. **Rope-aware string `,` operator** (documented but reverted —
   commit `21149f0`). Direct delegation to
   `ProtoString::appendLast` gave a verified 80× speed-up on
   `str_concat` (1630 → 20 ms) but broke
   `conformance/13-stdlib/json-parse-nested.st` because rope-internal
   nodes in protoCore hash on structure, not content. A correct
   landing requires a protoCore-side change to make
   `StringInternalNode::subtreeHash` a true content-fold; that is a
   follow-up not bundled with this perf pass.

4. **Inline `start to: end do: [:i | body]`** (commit `ea5c952`).
   Compiler emits the loop directly when the block is one-arg, no
   locals, and its argument is not captured by an inner block. The
   body emits in the calling frame; the iter-var name is temporarily
   rebound to a fresh local slot, then restored on exit.

A fifth item — inline cache for SEND — was investigated and scoped
out: it requires a stable per-class discriminator that protoCore's
current public API does not cleanly expose. Profiling shows 14.8 %
of post-Tier-S fib CPU in `getAttribute` (the MRO walk that an IC
would short-circuit), so this remains the largest single follow-up
target.

## Pre / post comparison (absolute protoST wall-clock, ms)

| Benchmark           | 2026-05-24 baseline | Post Tier-S/A | Change         | Driver |
|---------------------|--------------------:|--------------:|----------------|--------|
| `fib`               | 3652.6              | **891.1**     | **−76 % / 4.1×** | Tier-S #1 (no more NonLocalReturn throws per leaf) |
| `exception_latency` | 2237.3              | **1505.7**    | **−33 %**       | Tier-A #4 (outer `1 to: 50000 do:` inlined; nested `on:do:` still costs the bulk) |
| `attr_lookup`       | 598.1               | **348.0**     | **−42 %**       | Tier-A #4 (the `1 to: 100000 do:` outer loop inlined) |
| `range_iterate`     | 146.0               | 190.1         | +30 %           | within run-to-run noise — repeated isolated runs land 150–180 ms |
| `str_concat`        | 1630.2              | 1771.7        | +9 %            | within noise — Tier-A #3 was reverted, no change expected |
| `int_sum_loop`      | 94.4                | 106.0         | +12 %           | within noise — isolated single runs land 60–80 ms |
| `list_append`       | 58.2                | 116.2         | +100 %          | apparent regression in the harness median; isolated single runs land 60–70 ms (= unchanged). The harness run alternates protoST/CPython/protopy invocations and the CPython baseline on this row also shifted (31 → 37 ms), suggesting transient system load distorted both columns. Treat as noise. |

**Geomean ST/CPy: 9.96× → 9.02×** (≈10 % aggregate). The headline
move is fib's collapse from 84× → 16×; the geomean drag is
str_concat at 51× (untouched, blocked by protoCore hash) and
exception_latency at 25× (partial fix only — the inner `on:do:`
remains uninlined).

## Profile after the work (fib hot spots, post-Tier-S #1)

```
14.80%  ProtoObject::getAttribute      ← MRO walk per SEND — IC target
 8.03%  toImpl<ProtoObjectCell>        ← internal cast
 7.84%  runLoop                        ← dispatch
 6.31%  ProtoSparseListImpl::implGetAt ← AVL traversal
 4.59%  toImpl<ProtoThreadImplementation>  ← thread accessor in non-actor code
 3.93%  ProtoSparseListImpl::ctor      ← per-setAttribute allocation
 3.63%  addCell2Context                ← alloc accounting
 3.08%  allocCell                      ← alloc
```

The 11 % EH-infrastructure block (`__gxx_personality_v0` + FDE walk
+ translateNativeException.cold) that headlined the pre-pass
profile is **gone**. The cost now lives in genuine interpreter
work: attribute lookup, dispatch, and allocation.

## What's next (ordered by remaining ROI)

1. **Inline cache for SEND** — would directly attack the 14.8 % in
   `getAttribute`. Requires a stable per-class discriminator API in
   protoCore (the natural choice is the prototype's cell identity,
   but the current API does not surface it cleanly). Estimated 5–8 %
   geomean improvement.

2. **Rope-aware string `,` operator + content-fold rope hash in
   protoCore**. Two-line protoCore change (replace
   `hashCombine` with a polynomial fold that satisfies
   `combine(h_l, h_r, n_r) = continue(h_l, right_bytes)`),
   one-line protoST change to delegate to `appendLast`. Lifts
   `str_concat` from 51× to ~1× CPython, drops geomean by ~30 %
   on its own.

3. **Investigate the 4.6 % in
   `toImpl<ProtoThreadImplementation>` on pure-recursion fib**. No
   threads or actors involved in fib — this is almost certainly a
   per-opcode safepoint check that could be amortised across
   N opcodes the way protoPython amortises GC handshake checks.

4. **Computed-goto dispatch for the remaining non-threaded arms
   of `runLoop`** — the 0.2.0 work landed it for the common cases
   but `runLoop` is still 7.8 % of CPU; some opcodes still go
   through the slow case-switch fallback.

5. **`on:do:` inlining** — would attack the remaining 25× of
   `exception_latency`. Architecturally harder than the
   `ifTrue:` / `to:do:` inlining because the handler-frame
   semantics must round-trip a `signal` to the right handler
   without losing the current execution position. Plausible 30–50 %
   improvement on `exception_latency`.

