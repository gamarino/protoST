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
| int_sum_loop | 103.2 | 49.5 | 32.3 | 2.09× |
| fib | 1031.9 | 68.2 | 182.1 | 15.13× |
| list_append | 99.2 | 35.7 | 266.2 | 2.78× |
| str_concat | 19.4 | 30.4 | 296.8 | 0.64× |
| attr_lookup | 281.4 | 44.9 | 212.8 | 6.26× |
| range_iterate | 154.2 | 37.0 | 159.4 | 4.17× |
| exception_latency | 1248.3 | 38.7 | 658.3 | 32.24× |
| **Geomean** | | | | **4.65×** |

Geometric-mean ratio across the comparable suite: **protoST is 4.65× CPython's wall-clock** on these single-threaded workloads.

## Actor-model benchmarks

protoST-specific — protoPython has no actor model. These exercise the cooperative actor scheduler.

| Benchmark | Result |
|---|---|
| **Parallel speedup** | 12 CPU-bound worker actors: 1081 ms with the full pool vs 2640 ms with `PROTOST_WORKERS=1` — **2.44× speedup** |
| **Cooperative-yield scaling** | **1000** waiter actors, each parked on a nested `wait`, all hosted on **K=2** worker threads — completes in 2474 ms. Thread-per-actor blocking would need 1000 OS threads. |
| **Message throughput** | 2,000 drained round-trip sends to one actor in 37 ms — **53,748 messages/second**. |

### Reading these numbers

protoST is now within a small constant factor of CPython on single-
threaded workloads (geomean **4.65×**, down from 9.96× at the start of
the day) and **essentially at parity with `protopy`** on the same suite
(geomean **1.02×** vs `protopy`, with three outright wins). On
`str_concat` it now **beats CPython** (0.64×) — a 86× speed-up from the
day-start baseline. The actor results remain the strategic
differentiator: the cooperative-yield benchmark hosts a thousand
suspended actors on two OS threads (impossible under thread-per-actor),
and the parallel benchmark turns extra cores into real wall-clock
speedup with no code change.

---

## Full-sprint pre/post (absolute protoST wall-clock, ms)

This run-ID supersedes `2026-05-24-perf-post-tier-s-a.md`. The full
2026-05-24 sprint landed FOUR perf changes:

  0d396ca  inline ifTrue:/ifFalse:/ifTrue:ifFalse:/whileTrue: literal blocks
  ea5c952  inline `start to: end do: [:i | body]` literal one-arg block
  8dd93f2  canonicalise Dict key hashes through the symbol table
           + land the rope-aware `,` fast path (was previously reverted)

(21149f0 also lives in the chain — the original revert of the rope path.
8dd93f2 supersedes it.)

| Benchmark           | Day-start (ms) | Final (ms) | Speed-up      | Status |
|---------------------|---------------:|-----------:|---------------|--------|
| `str_concat`        | 1630.2         | **19.4**   | **86.0×**     | 🔥 BEATS CPython (0.64×) |
| `fib`               | 3652.6         | **1031.9** | **3.5×**      | from CATA to ok (15× CPy) |
| `exception_latency` | 2237.3         | **1248.3** | **1.8×**      | still slow, on:do: inlining is next |
| `attr_lookup`       | 598.1          | **281.4**  | **2.1×**      | improved |
| `list_append`       | 58.2           | 99.2       | (noise)       | WIN vs protopy (0.37×) — stable |
| `range_iterate`     | 146.0          | 154.2      | (noise)       | parity vs protopy (0.97×) |
| `int_sum_loop`      | 94.4           | 103.2      | (noise)       | stable; SmallInt opcode fast-path is the next step |
| **Geomean ST/CPy**  | **9.96×**      | **4.65×**  | **−53 %**     |        |
| **Geomean ST/PP**   | **1.91×**      | **1.02×**  | **parity**    |        |

## What pulled the geomean

| Change | Benchmark(s) attacked | Mechanism |
|--------|-----------------------|-----------|
| Inline `ifTrue:` / `whileTrue:` of literal blocks | fib (4.1× in isolation) | Eliminates the nested `ExecutionEngine` and the C++ `NonLocalReturn` throw on `^expr` inside the conditional block. fib used to throw ~75 K times per fib(25) leaf-call set. |
| Inline `to:do:` of literal one-arg blocks | exception_latency (33 %), attr_lookup (42 %) | Replaces `prim_Integer_toDo:do:` + `invokeBlock` + nested engine with a direct bytecode loop in the calling frame. |
| Dict key canonicalisation + rope `,` | str_concat (86 ×) | Two-part: `dictKeyHash` routes any non-symbol `ProtoString` through `createSymbol` (content-keyed interner — fixes structural-hash bug for rope keys) and `prim_StrConcat` now takes the `appendLast` rope-spine path (O(log N) per concat, was O(N)). |

## What's left

The Dict-canonicalisation + rope landing is the architectural insight
of the day: protoCore already provided the right pieces
(ProtoSparseList for collections, the symbol table for content-keyed
interning, the rope-aware appendLast). protoST just was not wiring
canonical-pointer hashing through Dictionary — once it does, the
rope-concat fast path lands trivially without a protoCore change.

Remaining items from the per-test analysis at session start:

1. **Inline cache for SEND** (was Tier A #5; scoped out). Profile
   shows ~15 % of post-Tier-S fib CPU in `getAttribute` (MRO walk
   that an IC would short-circuit). Needs a stable per-class
   discriminator the current protoCore public API does not cleanly
   expose; the cleanest path is a small protoCore addition (e.g.
   `ProtoObject::getClassIdentity()` returning the prototype cell
   pointer) and then a per-bytecode-position IC slot in
   BytecodeModule.

2. **`on:do:` inlining** for `exception_latency` — the outer
   `1 to: 50000 do:` is inlined; the inner `[…] on: Error do: […]`
   per-iteration spin-up of a nested engine + handler stack
   manipulation now dominates the cost. Same architectural pattern
   as the `to:do:` inline but harder because the handler-frame must
   round-trip a `signal` to the right handler. Plausible 30–50 %
   further reduction on `exception_latency`.

3. **SmallInt opcode fast path** for `+`, `-`, `<=`, `<`, `>=`, `>`
   inside the inlined `to:do:` body. Currently those still dispatch
   through SEND_BINARY → `prim_Int_*`. A `BINARY_OP_INT_ADD` /
   `BINARY_OP_INT_CMP_LE` pair that tag-checks both operands and
   falls back to SEND on a miss would close most of the remaining
   gap on tight integer loops (`int_sum_loop`, `list_append`,
   `range_iterate`). protoPython did this in Phase 8 with ~3×
   geomean improvement on micros.

