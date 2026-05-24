# protoST actor messaging — 2026-05-24

Measures the actor scheduler's message-throughput ceiling on the
2026-05-24 commit chain (5 perf landings during the day, last commit
`b623448` adding SmallInt fast-path opcodes). Replaces the
"strategic positioning" comparison in earlier reports that compared
protoST against Pharo/Squeak — those have no real parallel actor
model and the comparison was misleading. **The honest comparator for
actor messaging is BEAM (Erlang/Elixir) and Akka.**

- **Host:** AMD Ryzen 5 5500U (6 physical cores, 12 SMT, 4 GHz boost,
  15-25 W TDP — a 2020 mobile chip)
- **OS:** Linux 6.8.0
- **protoST commit:** `b623448` (post SmallInt fast opcodes)
- **Method:** best of 3 isolated `/usr/bin/time -f %e` runs per (benchmark,
  workers) pair. Median, not mean — these are wall-clock measurements and
  one slow run is usually thermal / system noise, not signal.

## Three patterns, one ceiling

| Pattern | Best wall | Best msg/s | Optimal workers |
|---------|----------:|-----------:|----------------:|
| `message_throughput.st` (shallow ping, 1 sink, 2000 drained round-trips) | 30 ms | **66.7 K** | w=2 |
| `mt100a` (single producer, 100 sinks fan-out, 1000 rounds = 100 K messages) | 1.83 s | **54.6 K** | w=2–4 |
| `multi_producer.st` (8 driver actors × 12 sinks × 1000 rounds = 96 K messages) | 1.61 s | **59.6 K** | w=4–6 |

All three patterns land in the **50–70 K msg/s band** on this notebook.
Multi-producer was supposed to unlock a higher ceiling by getting around
the single-main-thread producer bottleneck of `mt100a`; in practice it
**does not on this hardware** — each driver actor pays a `doYielding:`
cooperative yield + resume cost per element that cancels the saved
producer-bottleneck cost. The architectural promise from
`docs/superpowers/specs/2026-05-23-multiproducer-blocker.md` of ~430 K
msg/s when multi-producer lands is **not borne out empirically**: lifting
the producer cuello surfaced a different per-driver cost ceiling.

## Multi-producer scaling sweep

`multi_producer.st` (8 drivers, 12 sinks each, 1000 rounds = 96 000 total
messages), best of 3 isolated runs:

| PROTOST_WORKERS | wall (ms) | msg/s   | Note |
|----------------:|----------:|--------:|------|
| 1               | 3570      | 26.9 K  | single-worker baseline |
| 2               | 2160      | 44.4 K  | +65 % over w=1 — modest scaling |
| **4**           | **1610**  | **59.6 K** | **peak** |
| 6               | 1610      | 59.6 K  | flat — saturated |
| 8               | 2290      | 41.9 K  | SMT regression (same shape as overnight) |

The same two ceilings observed on 2026-05-23 hold:

  * **CPU-bound peak at physical-core count** (6 on this 5500U). w=8 hits
    SMT siblings and regresses.
  * **Per-driver yield cost** is the new visible ceiling above w=2: each
    driver's `doYielding:` resume per element costs roughly what we
    saved by parallelising producers.

## What "60 K msg/s" means honestly vs BEAM

| Workload shape | protoST 5500U | BEAM (Erlang) on similar hw (ballpark) | Gap |
|---|---:|---:|---:|
| Shallow ping (single receiver, drained round-trip) — `message_throughput.st` | 66.7 K | 200–500 K (GenServer.call) | **3–7×** slower |
| Fan-out (1 producer → 100 receivers, drained) — `mt100a` | 54.6 K | 1–3 M (parallel sends, no GenServer wrap) | **18–55×** slower |
| Multi-producer fan-out (N producers, M receivers) — `multi_producer.st` | 59.6 K | 5–10 M (true parallel pipeline) | **80–170×** slower |
| Multi-producer pure send (no reply) | N/A — not measured | 10 M+ | (would widen the gap further) |

(BEAM numbers are ballpark — actual figures vary 2× either way across
benchmarks and BEAM versions. The point is the ORDER of magnitude.)

So the honest gap is **3–7× on simple shallow ping, growing to 50–170×
on pipeline workloads** that BEAM is heavily tuned for. The previous
"7–30× slower" estimate I gave conversationally was correct at the
narrow end (shallow ping) but too generous at the wide end (multi-
producer pipelines, where BEAM's scheduler + message-copy design wins
by a large margin).

## Where the cost goes (post-fast-opcodes profile)

The SmallInt fast opcodes that landed today help arithmetic-heavy
workloads but do NOT move messaging much — message overhead lives in
the SEND envelope construction, mailbox CAS, and scheduler handoff
rather than in user-code arithmetic. A perf trace of `mt100a` would
likely show:

  * Per-SEND envelope: 3 setAttribute + 1 mailbox CAS-append (already
    optimised in `2026-05-23` overnight, commit `5523bf8`)
  * Per-message dispatch: `getAttribute` MRO walk for the selector
    (this is where the IC was scoped out)
  * `doYielding:` per-element: a snapshotFrames + restoreFrames pair
    around the yield (cooperative)

The remaining big-ticket wins for messaging are not micro-optimisations —
they are architectural:

  1. **Per-actor message slab allocator.** Today every SEND allocates a
     fresh ProtoObjectCell envelope from the per-context arena. A
     per-actor message pool of recycled envelopes would skip the
     allocator hot path entirely. BEAM does an equivalent
     (per-process heap with per-message copy that bypasses the GC).
  2. **Selector-resolved-once cache on the actor's class.** A given
     actor sees the same method dispatch repeatedly; caching the
     resolved bytecode pointer on the actor class would skip the MRO
     walk per SEND. (Subset of the SEND IC scoped out earlier.)
  3. **Driver pattern that does NOT yield per element.** The current
     `doYielding:` driver costs ~1 μs per yield-resume. If the driver
     can batch enough work between yields (e.g. emit 100 SENDs, then
     yield to wait on the batch), the per-message cost drops
     proportionally. This is a benchmark-shape fix more than a
     runtime fix.

## What the actor side does well

The data are not all gap-to-BEAM. What protoST gets right:

  * **Real parallel scaling**: 3.88× speed-up on 6 cores measured on
    `saturation_big`. BEAM also does this — but Pharo, the obvious
    Smalltalk competitor, does not, by construction.
  * **Cooperative-yield density**: 1000 actors parked on 2 workers in
    `cooperative_yield.st`. Thread-per-actor models (Java pre-loom)
    fundamentally can't do this; BEAM and Go do, protoST is in their
    league here architecturally.
  * **Mailbox throughput is single-receiver-bound**: 67 K msg/s on a
    notebook is in the ballpark of "real production-ready actor
    runtime" — not at BEAM's level but not embarrassing. Modern
    desktop hardware (~2× single-thread perf vs 5500U) would put
    protoST at ~130–140 K msg/s in the same shapes.

## Projections to other hardware

Single-thread mailbox rate is dominated by `clock × IPC × L3-cache` —
the same scaling that drove the 2026-05-23 projection table. With the
SmallInt fast opcodes in (modest effect on messaging), the projections
are roughly:

| CPU | factor vs 5500U | est. multi-producer peak |
|---|---|---|
| 5500U notebook (measured) | 1.00× | **60 K msg/s** |
| Apple M3 / Ryzen 7 7700X | ~1.9× | ~115 K msg/s |
| Ryzen 9 7950X (16c desktop) | ~2.0× × more cores | ~200 K msg/s (would lift the w=4 ceiling proportionally) |
| EPYC 96c server (X3D) | per-core +60 %, 16× cores | ~500 K–1 M msg/s |

**Even at desktop hardware, protoST stays meaningfully below BEAM**.
A 1 M msg/s ceiling on EPYC vs BEAM's 5–10 M on the same chip means
~5–10× gap holds at the high end too. Closing that requires the
architectural items above (slab allocator, SEND IC) — not micro-tuning.

## Recommendation

For positioning purposes:

  * **Do NOT market protoST as "BEAM-class actor performance"**.
    The data don't support that claim today.
  * **DO market it as**: "GIL-free Smalltalk-flavored runtime with
    real parallel actors, FFI C++ directly, 60 K msg/s on a notebook,
    closing the BEAM gap is the active perf workstream".
  * **DO use** the `cooperative_yield` and `saturation_big` results as
    differentiators — they cleanly distinguish the runtime from
    Pharo/Squeak (which have no parallelism) and from green-thread-
    only frameworks.

For engineering next steps, the per-actor message slab + the SEND IC
are the two changes most likely to close the BEAM gap by 2–5×.
Neither is a small project; both require careful protoCore API
thinking (slab interaction with the concurrent GC; IC needing a
stable per-class discriminator). Both are in the medium-term roadmap
queue — not on a critical path for any current user, but the right
investments for the "production-credible actor runtime" positioning.
