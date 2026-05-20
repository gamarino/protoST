# protoST examples

Demos that exercise the protoST runtime end-to-end.

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

### Limitations as of `f6v2-mt`

- Each actor's lock is held across the full message dispatch, not just the
  mailbox RMW. This serializes messages **to one actor** but does NOT
  serialize messages **across actors**. The demo exercises the latter.
- Self-sends (a method calling `self <selector>`) currently don't compile
  through the SEND fast-path — known issue, harmless for the demo.
- True massive concurrency (thousands of actors waiting on each other)
  needs cooperative yield from the bytecode interpreter — that's F6 v3.
