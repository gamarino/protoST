# Message throughput — re-baseline, 2026-05-22

This report covers one benchmark, `benchmarks/actors/message_throughput.st`
(drained round-trip sends to a single actor), re-measured after the
lock-free-scheduler work. The other benchmarks in
[`2026-05-21-baseline.md`](2026-05-21-baseline.md) were not re-run; the
comparable-suite and the other two actor benchmarks stand from that report.

## Numbers

Whole-process wall time (`/usr/bin/time`); runtime startup is ~10 ms,
negligible at these sizes. Each row is the median of repeated runs.

| Round-trips | before | after | rate before | rate after |
|---|---|---|---|---|
| 2,000   | 2.32 s   | 0.17 s  | ~865 msg/s | ~11,800 msg/s |
| 20,000  | 23.1 s   | 1.69 s  | ~866 msg/s | ~11,800 msg/s |
| 100,000 | 116.2 s  | 8.25 s  | ~861 msg/s | ~12,100 msg/s |

Both before and after are **linear in the message count** with negligible
startup — the per-message cost is constant, there is no super-linear
degradation. Headline: **~12,000 messages/second**.

## What "before" was — a diagnosed regression

The `2026-05-21-baseline.md` figure was **7,700 msg/s**. Between that report
and this one the de-locking work (lock-free actor mailbox + lock-free
`Future`, replacing the per-actor mutex and the per-future
mutex+condition-variable) landed. The lock-free `Future` replaced the wait
condition variable with a poll, and the non-actor `Future>>wait` path slept
a backed-off 1 ms..32 ms doing no scheduler work itself. A foreground thread
is not an actor — while it slept, only the worker pool advanced — so every
serial `(x) wait` round-trip paid at least the 1 ms first sleep. That capped
the benchmark near 1/firstSleep ≈ **865 msg/s**: a ~9× regression that the
2,000-send benchmark size partly masked.

## The fixes

Measured here as one step, two commits:

- **`9412576`** — the scheduler is lock-free: `schedMu` / `schedCv` / the
  `std::queue` ready queue / the `std::unordered_set` are gone, replaced by
  a CAS'd `ProtoList` and a per-actor 3-state `__sched__` flag.
  Perf-neutral on its own (it was not the regression) but it removes the
  last runtime mutex.
- **`05ae211`** — the non-actor `Future>>wait` now **drives `drainOne`**
  instead of sleep-polling. When the awaited future's actor turn is the
  waiting thread's to run, the whole round-trip completes on that thread
  with no sleep and no worker hand-off. This is the ~14× recovery — and it
  beats the pre-de-locking 7,700/s, because driving `drainOne` directly
  has no per-message worker-wakeup latency at all.

## Method

```
sed 's/1 to: 2000 do:/1 to: N do:/' benchmarks/actors/message_throughput.st
/usr/bin/time -f "%e" ./protost <file>      # repeated; median
```

Verified alongside: `ctest` 751/751 green across many runs (full suite and
the actor/scheduler subset), no hangs, all under a per-test timeout.
