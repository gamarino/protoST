# Interpreter performance — threaded-goto dispatch + envelope/Future flattening

**Status:** spec, ready to execute. Two staged efforts, each its own focused
session. Target: ~100,000 msg/s on `message_throughput.st`, the credibility
floor for the actor-runtime community (BEAM `gen_server:call` /
Akka `?` ballpark).

Current measured baseline (after the Bootstrap symbol-cache extension,
2026-05-23): **~18,000 msg/s** on a 100 K round-trip benchmark. The path to
~100 K is the two efforts below.

---

## Stage 1 — Threaded-goto dispatch in `runLoop`

### Goal
Replace the centralised switch dispatch in
`ExecutionEngine::runLoop` with computed-goto dispatch — one indirect jump
per opcode handler instead of a single shared indirect jump at the switch
end. Expected: 20-40% reduction in interpreter-loop wall time on the
benchmark.

### Why this is its own focused session
- `runLoop` is **~1,223 lines**, **32 case arms**, **34 `break;`** sites,
  **9 `continue;`** sites. Each `break;` / `continue;` must be classified
  (dispatch-level vs inner-loop) before conversion.
- The interpreter is the most densely-tested code in the runtime. A subtle
  dispatch bug is intermittent — passes ctest sometimes, fails others.
- Verification needs **many ctest runs** (multi-threaded + actor-stress) to
  trust the result.
- Mid-conversion the file does not compile (a mix of switch arms and labels
  is not valid — the labels table references forward labels that only exist
  after all cases are converted). It is an **atomic refactor**.

### Design — the "hybrid" form

The cleanest minimal form keeps the existing `switch` as the first-entry
dispatch and adds threaded-goto for every subsequent dispatch:

1. **Function-scope `op` and `arg`** (currently per-iteration locals). All
   reads/writes of `op` and `arg` route through these.
2. **Labels table** at the top of `runLoop`:
   ```cpp
   static const void* const labels[256] = {
       [0 ... 255] = &&L_BAD_OP,
       [Op::NOP]            = &&L_NOP,
       [Op::PUSH_CONST]     = &&L_PUSH_CONST,
       ... (all 32 opcodes)
       [Op::SEND_UNARY ... Op::SEND_SUPER] = &&L_SEND_GROUP,
   };
   ```
3. **`DISPATCH_DIRECT()` macro** — replicates the outer-while's prelude
   (rebind frame, off-end check, debugger check, EXTEND fetch, opcode/arg
   read) and ends with `goto *labels[op]`.
4. **Each `case Op::X:`** gets an `L_X:` label at its top **and** a shadow
   `Frame& f = frames_.back();` (the outer-while's `f` is stale once the
   threaded gotos bypass the loop iteration cycle).
5. **Each dispatch-level `break;`** is preceded by `DISPATCH_DIRECT();` —
   the goto fires first and the break becomes unreachable.
6. **Each dispatch-level `continue;`** (e.g. `RETURN_TOP`, the `RETURN`
   non-local-unwind paths, the block-invocation path of `SEND_*`) is
   replaced by `DISPATCH_DIRECT();`.
7. **End-labels** added at the bottom of the `try`:
   - `L_OFF_END:` — runs the existing off-the-end fallthrough then
     `DISPATCH_DIRECT();`.
   - `L_BAD_OP:` — `throw std::runtime_error("bad opcode");`.
   - `L_RUN_DONE:` — `return PROTO_NONE;` (`frames_.empty()` safety net).

### What stays
- The outer `while (true) { try { ... } catch (DebuggerHalt) { ... } }`
  stays. A `continue` at the bottom of the catch handler re-enters the try
  and the initial switch dispatches again.
- `snapshotFrames` / `restoreFrames` work unchanged — `frames_` is still
  the cooperative-yield snapshot target.
- The DAP debugger reads `frames_` unchanged.

### Risks
1. **Inner-loop `break;` / `continue;`** misclassified as dispatch-level →
   wrong control flow. Mitigation: read each case body carefully before
   touching its breaks.
2. **`Frame& f` shadowing across gotos** — the goto must not skip a
   non-trivial-init declaration. References are trivial init in C++ →
   skipping is allowed. Inner shadow `Frame& f = frames_.back();`
   re-initialises at the goto target.
3. **EXTEND opcode** — the prefix loop must stay inside `DISPATCH_DIRECT`
   so a goto'd-into label sees the fully-decoded op + arg.

### Verification
- Full `ctest` × at least **5 runs** (the actor / yield / SEND tests are
  the ones that will catch subtle dispatch bugs).
- `cli_actor_stress` × 3 runs (D23 territory — the scheduler + interpreter
  under sustained load).
- The actor-subset tests hammered × 10.
- `message_throughput` measurement (mt2k / mt20k / mt100k for linearity).

### Expected impact
~20-40 % reduction in interpreter loop wall time. `mt100k` 5.5 s -> 3.5-4.5 s,
yielding **~22-28 K msg/s**.

---

## Stage 2 — Flatten the message envelope and the `Future`

### Goal
Re-represent the per-message bookkeeping as flat C++ structs with atomic
fields instead of mutable `ProtoObject`s with multiple `setAttribute`
calls per message. Cut ~10-15 mutable-attribute rebuilds per round-trip
to ~2-3 atomic stores. Expected: a second 2-3× on top of Stage 1.

### Background
The static analysis identified the per-message machinery as the structural
performance lever:
- **Future** has 7 attributes (`__state__`, `__value__`, `__error__`,
  `__then_cbs__`, `__catch_cbs__`, `__waiters__`, `__settling__`). Settling
  does 5+ `setAttribute` calls on a mutable object — each a snapshot
  rebuild + shard CAS.
- **Message envelope** has 3 attributes (`__selector__`, `__args__`,
  `__future__`) set during SEND. Three `setAttribute` calls per send.
- **Mailbox** is a `ProtoList` rebuilt on append (SEND) and pop (drainOne).

Total: ~10-15 mutable-attribute rebuilds per round-trip — most of the
per-message Cell allocation traffic (and therefore most of the GC and
allocation work, even though those are concurrent / largely off-critical).

### Design

Two flat C++ types, owned and pooled by the runtime:

```cpp
struct MessageEnvelope {
    const proto::ProtoString* selector;     // pre-interned (immutable across life)
    const proto::ProtoObject* args;         // protoList (already GC-rooted via mailbox / future)
    Future*                   future;       // owned C++ struct (see below)
    MessageEnvelope*          mailboxNext;  // intrusive linked list — the mailbox
};

struct Future {
    std::atomic<int>          state;        // 0 pending / 1 resolved / 2 rejected
    std::atomic<const proto::ProtoObject*> value;
    std::atomic<const proto::ProtoObject*> error;
    /* intrusive lists for then-cbs, catch-cbs, waiters — atomic head */
    std::atomic<Future*>      thenCbHead;   // linked via "next" field on each callback
    std::atomic<Future*>      waiterHead;   // linked list of suspended actors
    std::atomic<bool>         settling;     // CAS claim flag
};
```

The user-facing `Future` ProtoObject becomes a thin **facade** carrying
just a `__c_future__` pointer (an opaque pointer to the C++ struct). User
code (`x then:` / `x wait` / `x value`) routes through primitives that
read/write the C++ struct directly via atomics.

### Why this works
- A round-trip's per-message machinery becomes ~3 atomic stores
  (state, value, settling-claim) instead of ~15 sparse-list rebuilds.
- Allocation count per message drops 5-10× → GC pressure drops
  proportionally (still concurrent, so wall-clock impact is bounded but
  real on the critical path's allocCell time).
- The mailbox becomes an **intrusive lock-free MPSC** — append by atomic
  CAS on `mailboxHead`, pop by atomic XCHG / single-consumer drain. No
  ProtoList rebuilds.
- The user-visible API is unchanged.

### What needs GC-rooting
The MessageEnvelope and Future C++ structs are NOT `ProtoObject`s — the GC
does not trace them. They hold `ProtoObject*` (selector, args, value,
error). To root those, the runtime maintains a per-runtime **pool object**
(a pinned mutable `ProtoObject`) whose attributes form a flat set of
the live envelopes/futures. The pool object is reached from the live
registry / async roots; its attributes (containing the live envelope's
ProtoObject pointers) are traced.

Alternatively, the live envelopes are reachable via the receiving actor
(mailbox) or the awaiting actor (waiter list); a careful audit of every
escape path can guarantee the C++ structs are always reachable from at
least one ProtoObject root. This needs writing as an invariant.

### Risks
1. **GC-rooting** — the C++ structs are invisible to the GC. The
   `ProtoObject*` they hold must each be reachable from at least one GC
   root at every moment. A missed rooting is a silent use-after-free —
   the canonical hard bug.
2. **API compat** — the user-facing `Future` object must continue to
   answer the existing protocol (`then:`, `wait`, `value`, `error`,
   `settle:`, `reject:`, the `Atom`-style `setAttribute` introspection).
   The facade design preserves it.
3. **Snapshot/restore (cooperative yield)** — the Future's state is now in
   a C++ struct, not the facade ProtoObject. A snapshot of an actor parked
   on a Future must capture the C++ Future pointer (the facade) and the
   actor's resume info. Should be straight — the snapshot already targets
   the actor's mailbox / awaited Future facade.

### Verification
- Full `ctest` x 5 runs.
- `cli_actor_stress` x 3.
- The cooperative-yield benchmark (`cooperative_yield.st`) — 1,000 actors
  parked on nested waits — runs at unchanged rate.
- `parallel_speedup.st` runs at unchanged rate.
- `message_throughput.st` measured; expected ~50-100 K msg/s when combined
  with Stage 1's threaded-goto.

### Expected impact
- Per-message Cell allocations: ~60-90 -> ~10-15.
- GC garbage rate: ~40 MB/s -> ~5 MB/s (concurrent, but reduces allocCell
  on the critical path).
- mt100k: 3.5-4.5 s (post-Stage-1) -> **~1-2 s -> ~50-100 K msg/s**.

---

## Combined target

| | mt100k | msg/s | factor |
|---|---|---|---|
| Pre-perf-arc (de-locking regression) | ~115 s | ~865 | 1.0× |
| Post-wait-fix + symbol caches | ~6.3 s | ~15,800 | 18× |
| **Today (+ home/block symbol cache)** | **~5.5 s** | **~18,200** | **21×** |
| + Stage 1 (threaded-goto) | ~3.5-4.5 s | ~22-28K | ~27× |
| + Stage 2 (flatten envelope/Future) | ~1-2 s | **~50-100 K** | ~60-115× |

At ~50 K msg/s protoST is in BEAM/Akka territory on this benchmark; at
~100 K it crosses the credibility floor identified in the previous session
("estamos en carrera").
