# Ready-queue: intrusive lock-free stack — fix the GIL-equivalent

**Status:** spec + implement, 2026-05-23. Targets the no-scaling bug
diagnosed in `2026-05-23-interpreter-perf-stage1-report.md` and
confirmed experimentally with `perf stat` on `mt8w_micro`.

## The bug — restated

The ready queue is `liveRegistry.__ready__`, a `ProtoList` (immutable
structural-sharing). `enqueueReady` / `dequeueReady` are CAS-retry loops
that **rebuild the list on every operation** — `appendLast` and
`getSlice(1, n)` each allocate a fresh `ProtoList`. With 8 workers +
main thread polling `Future::wait` (which also drives `drainOne`), all 9
threads contend on a single attribute and each retry allocates garbage.

`perf stat` on `mt8w_micro` (4000 msgs, 5 repeats, 1 vs 8 workers):

| | 1w | 8w | Ratio |
|---|---|---|---|
| Wall | 0.250 s | 0.255 s | 1.02× |
| Instructions | 1972 M | 2007 M | **1.018×** |
| Context-switches | 474 | 2789 | **5.88×** |
| CPU-migrations | 10 | 206 | **20.6×** |
| IPC | 2.00 | 1.78 | 0.89× |

+25 % CPU-time produces +1.7 % useful instructions. The other 23 % is
synchronisation pure. **GIL-equivalent behaviour.**

## Design

Two structures, with separated concerns:

### 1. `ReadyStack` — intrusive lock-free Treiber stack (C++)

Per-runtime C++ struct. Push/pop are CAS over a single head pointer.
Operations are O(1), allocate one `ReadyNode` per push (`new`), free on
pop (`delete`). No rebuild, no structural sharing, no `ProtoList`
involved. LIFO order — we accept this for the first iteration; FIFO
fairness (Michael-Scott) is a follow-up if needed.

```cpp
struct ReadyNode {
    const proto::ProtoObject* actor;
    ReadyNode*                next;
};

class ReadyStack {
    std::atomic<ReadyNode*> head_{nullptr};
public:
    void push(const proto::ProtoObject* actor);
    const proto::ProtoObject* pop();   // nullptr if empty
};
```

ABA: not addressed in this iteration. The node is `delete`d on pop, so a
recycled allocation can in principle return to head; the CAS will see a
matched `old` but a different `next`. Mitigated in practice by glibc
malloc not immediately recycling freed pointers; risk is the rare
spurious pop. Will be hardened later with tagged pointers or hazard
pointers.

### 2. `liveActors` — `ProtoList` anchor for GC liveness (rooted)

The C++ `ReadyNode` is not traceable by the protoCore GC. An actor
referenced only from a `ReadyNode` would dangle. Solution: anchor every
actor that has ever entered the ready queue in a `ProtoList` under
`liveRegistry.__live_actors__`, idempotently on first enqueue.

Per-actor `__live__` flag (PROTO_TRUE/FALSE/absent) is CAS'd from absent
or FALSE to TRUE on the first enqueue. The CAS winner appends the actor
to `__live_actors__`. Later enqueues see TRUE and skip the anchor step.

**First iteration: actors are never removed from `__live_actors__`.**
This is a leak on long-running runtimes (actors stay reachable after
their last reference). Acceptable for the benchmark targets and the
common case where actor populations are bounded. A future iteration
will add a "drop anchor when truly idle" pass with double-checked CAS.

### Why this fixes the bug

`enqueueReady` becomes:
- Anchor (idempotent, rare — once per actor lifetime): 1 attribute read
  + 1 conditional CAS + 1 ProtoList rebuild **on the first call only**.
- Schedule (per send): `new ReadyNode + 1 CAS on head_`. **O(1), no
  attribute access.**

`dequeueReady` becomes: `1 CAS on head_ + delete ReadyNode`. O(1).

Contention on `head_` is a single cache line bouncing between cores — a
hardware cost ~50 ns per CAS, not the ~µs cost of allocating + rebuilding
a `ProtoList`. The slope from 1 → 8 workers should be near-linear up to
the point where the head-cache-line bouncing dominates.

## What does NOT change

- `Future::wait` still drives `drainOne` from the main thread. Keeps
  the mt100k single-actor case fast (~20 K msg/s). The contention there
  is now harmless because the ready-stack pop is genuinely O(1).
- Mailbox is still `ProtoList` with CAS-retry rebuild. That is the next
  bottleneck after this fix (Stage 2 of the original spec).
- Future settlement is unchanged. Same.
- protoCore is unchanged. v1.1.0 / v0.1.0 pair stays valid.

## Files touched

| File | Change |
|---|---|
| `src/runtime/Bootstrap.h` | 2 new cached symbols: `live`, `liveActors` |
| `src/runtime/Bootstrap.cpp` | Intern the 2 new symbols |
| `src/runtime/STRuntime.h` | Forward-declare `ReadyStack`; field in `Impl` |
| `src/runtime/STRuntime.cpp` | New `ReadyStack` C++ class; rewrite `enqueueReady` / `dequeueReady`; init `liveActors` attribute on `liveRegistry` |

Net diff: ~ 80 lines.

## Verification

- Full ctest (multi-thread + actor tests).
- `mt8w` (drained depth=1) before / after — must show real scaling
  (msg/s increases with `PROTOST_WORKERS`).
- `mt100k` before / after — must not regress.
- `perf stat` on `mt8w_micro` — context-switches and CPU-migrations
  must drop dramatically. Instructions ratio (8w/1w) should stay ≈ 1
  but wall-time ratio should drop below 1 (true speedup).
- All under cgroup MemoryMax=4G — never run unsafe again.

## Expected outcome

If the diagnosis is correct: `mt8w` with 8 workers should approach 8×
the throughput of 1 worker, capped by Amdahl's main-thread serial work
(the per-batch `actors do: [ :a | futures add: (a ping) ]` is still
serial in the script). Realistic ceiling: 30-60 K msg/s on the deep
variant if workers genuinely parallelise. Single-actor mt100k unchanged
(~20 K msg/s).

If the diagnosis is incomplete (second bottleneck in GC / globalMutex /
mailbox): scaling improves but not linearly. We measure and decide
whether to extend the fix into Stage 2.
