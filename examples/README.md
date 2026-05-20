# protoST examples

Demos that exercise the protoST runtime end-to-end.

> **Debugging:** these scripts can be debugged in VS Code via the protoST DAP
> adapter. See [`../docs/debugging.md`](../docs/debugging.md) for setup; a
> ready-to-use `launch.json` is in [`.vscode/launch.json`](.vscode/launch.json).

## `pump_twin.st` — Digital-twin with parallel sensors

A minimal industrial digital twin: one pump (FSM with `off` / `running` states)
and three sensors (temperature, flow, vibration). Each sensor read has a
simulated 50 ms I/O latency.

The Controller runs `cycle`s — each cycle fires the 3 sensor reads, gets back
3 Futures, then waits on all 3. Because each sensor is wrapped as an actor,
the runtime schedules them on different workers and the reads execute in
parallel.

### Run

```bash
# Default: workers = min(hardware_concurrency, 8)
time ./build/protost examples/pump_twin.st
# Expected: returns "3", wall-clock ~200ms

# Forced serial (proof that parallelism is what's saving time)
time PROTOST_WORKERS=1 ./build/protost examples/pump_twin.st
# Expected: returns "3", wall-clock ~500ms
```

The ~2.5× speedup (200 ms vs 500 ms) is real parallelism across OS threads —
the runtime spawns N managed `ProtoThread` workers (T7 of F6 v2) and each
sensor's `read` message runs on a different worker.

### The "digital-twin" pattern this demonstrates

Each physical component (pump, sensors) becomes its own actor with:
- its own mailbox (private state, serialized message processing),
- its own protoCore mutex (no shared-memory races),
- independent scheduling (the runtime decides which worker runs which message).

The Controller is a regular object (not an actor) that **fans out** to its
component actors. This is the inverse of a traditional FSM: instead of one
big state machine, you compose many small state-holders that react to
messages independently.

Adding more pumps, alarms, loggers, persistence, etc. is just adding more
actors — no runtime changes needed.

### Notes

- Each actor's lock is held across the full message dispatch, not just the
  mailbox RMW. This serializes messages **to one actor** but does NOT
  serialize messages **across actors** — the demo exercises the latter
  (the three sensors run in parallel on different workers).
- A plain self-send inside a handler (`self foo`) dispatches directly:
  `self` is the wrapped base object, not the actor proxy, so it never
  touches the mailbox or the actor lock.
- Cooperative yield (F6 v3) lets an actor awaiting a Future release its
  worker thread, so the runtime scales to far more concurrent interdependent
  actors than it has OS threads.
- Sending a message to the *same* actor through its actor reference from
  inside one of its own handlers is not supported (it would re-enter the
  non-recursive actor lock).
