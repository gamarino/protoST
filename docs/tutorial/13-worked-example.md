# Chapter 13 — A worked example

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 12](12-tooling.md) · Next: [Chapter 14 — For the Smalltalk programmer](14-for-the-smalltalk-programmer.md)

---

Every chapter so far built understanding one feature at a time. This chapter
builds a *program*. We will walk through `examples/pump_twin.st` — a complete,
runnable digital twin of an industrial pump — line by line, and watch every
idea in the tutorial come together: classes, instance variables, methods,
`self`, actors, futures, and the fan-out/join concurrency pattern.

The example is in the repository. Run it as we go:

```bash
$ ./build/protost examples/pump_twin.st
3
```

## 13.1 What we are building

A **digital twin** is a software model of a physical thing that holds its state
and reacts to its events ([Chapter 1](01-introduction.md)). The thing here is
an industrial pump fitted with three sensors:

- a **temperature** sensor,
- a **flow** sensor,
- a **vibration** sensor.

The pump itself is a small finite state machine that toggles between `off` and
`running`. A **controller** owns the pump and the three sensors and runs
*cycles*: each cycle reads all three sensors and aggregates the readings.

The interesting part is concurrency. A sensor read models real I/O latency —
50 milliseconds — with a `sleep`. Three reads done one after another would cost
150ms. But if each sensor is an **actor**, the controller can fire all three
reads at once and they run in parallel, on different worker threads, for a
wall-clock cost of about 50ms. That speedup is the whole point of the digital-
twin-as-actor-swarm idea, and we will measure it.

## 13.2 The pump — a finite state machine

The pump is an ordinary object. It has two instance variables: `state` (the
string `'off'` or `'running'`) and `ticks` (a running count).

```smalltalk
"-- Pump: an FSM that toggles between 'off' and 'running' --"
Object subclass: #Pump
  instanceVariableNames: 'state ticks'.

Pump >> initialize
  state := 'off'.
  ticks := 0.

Pump >> start
  Object sleep: 30.
  state := 'running'.
  ^ state.

Pump >> stop
  state := 'off'.
  ^ state.

Pump >> tick
  state = 'running' ifTrue: [ ticks := ticks + 1 ].
  ^ ticks.

Pump >> state  ^ state.
Pump >> ticks  ^ ticks.
```

Everything here is [Chapter 5](05-classes-and-methods.md) material. `Pump` is
declared with two instance variables. `initialize` sets them — and recall from
Chapter 5 that `new` does *not* call `initialize`, so the main program will
have to send it explicitly. `start`, `stop`, and `tick` are the FSM
transitions; `start` simulates 30ms of spin-up latency with `Object sleep: 30`.
`tick` uses an `ifTrue:` conditional — a message on the boolean `state =
'running'`, exactly as [Chapter 4](04-blocks.md) described — so it only counts
ticks while the pump is running. `state` and `ticks` are accessor methods.

> Notice the `Pump` is *not* an actor. It is a plain object — the controller
> will hold it directly. Not every component of a digital twin needs to be an
> actor; you make something an actor when you want its messages serialised and
> schedulable. The pump here is driven only by the controller, one message at a
> time already, so a plain object is the right choice.

## 13.3 The sensors — three objects with simulated latency

The three sensors are near-identical small classes. Here is the temperature
sensor; flow and vibration are the same shape with different numbers:

```smalltalk
"-- Three independent sensors, each with simulated I/O latency --"
Object subclass: #TempSensor
  instanceVariableNames: 'reading'.

TempSensor >> initialize  reading := 20.

TempSensor >> read
  Object sleep: 50.
  reading := reading + 3.
  ^ reading.

Object subclass: #FlowSensor
  instanceVariableNames: 'reading'.

FlowSensor >> initialize  reading := 0.

FlowSensor >> read
  Object sleep: 50.
  reading := reading + 7.
  ^ reading.

Object subclass: #VibSensor
  instanceVariableNames: 'reading'.

VibSensor >> initialize  reading := 0.

VibSensor >> read
  Object sleep: 50.
  reading := reading + 1.
  ^ reading.
```

Each sensor holds one instance variable, `reading`. Each `read` method does
three things: it `sleep`s for 50ms — standing in for the latency of talking to
real hardware — then advances its reading and returns it. The 50ms sleep is the
crux of the whole demonstration: it is the cost we are going to parallelise
away.

## 13.4 The controller — fan out, then join

The controller is where the concurrency lives. It owns the pump and the three
sensors, and its `cycle` method is the heart of the example.

```smalltalk
"-- Controller: owns the pump + sensors, runs cycles --"
Object subclass: #Controller
  instanceVariableNames: 'pump temp flow vib cycles'.

Controller >> initWith: p temp: t flow: f vib: v
  pump := p.
  temp := t.
  flow := f.
  vib := v.
  cycles := 0.

Controller >> cycle
  | ft ff fv tr fr vr |
  "Fire all 3 reads — each returns a Future immediately"
  ft := temp read.
  ff := flow read.
  fv := vib read.
  "Now wait for all 3 — they ran in parallel on different workers"
  tr := ft wait.
  fr := ff wait.
  vr := fv wait.
  cycles := cycles + 1.
  ^ tr + fr + vr.

Controller >> cycles  ^ cycles.
```

`initWith:temp:flow:vib:` is a four-keyword method that stores the pump and
the three sensors, and zeroes the cycle count.

`cycle` is the method to study closely. It declares six method temporaries with
`| ft ff fv tr fr vr |` — the three *futures* and the three *results*. Then:

**The fan-out.** The first three statements —

```smalltalk
ft := temp read.
ff := flow read.
fv := vib read.
```

— send `read` to each sensor. But `temp`, `flow`, and `vib` here are **actors**
(the main program wraps them — §13.5). So, as [Chapter 10](10-actors-and-futures.md)
taught, `temp read` does *not* block for 50ms. It returns a `Future`
immediately and the sensor's worker starts the read. By the time all three
statements have run — which takes essentially no time — all three sensor reads
are *already running in parallel*, on three different worker threads.

**The join.** The next three statements —

```smalltalk
tr := ft wait.
fr := ff wait.
vr := fv wait.
```

— `wait` on each future, collecting the resolved readings. The first `wait`
blocks until the temperature read finishes (~50ms). But by the time it returns,
the flow and vibration reads — which started at the same moment — are finished
*too*, so `ff wait` and `fv wait` return almost immediately.

The whole `cycle` therefore costs about 50ms of wall-clock time, not 150ms.
Three sequential reads collapsed to one read's duration, because they ran at
once. This is the **fan-out / join** pattern: start everything (collecting
futures), *then* wait for everything. It is the single most important
concurrency idiom in protoST, and `cycle` is six lines of it.

> Contrast the wrong way: `tr := (temp read) wait.` then `fr := (flow read)
> wait.` — sending and immediately waiting, one at a time. That serialises the
> reads back to 150ms. The fix is exactly what `cycle` does: separate the
> sends from the waits.

## 13.5 The main program — wiring it together

The top-level forms of the file build the twin and run it:

```smalltalk
"-- MAIN PROGRAM --"

pump := Pump newChild.
pump initialize.
pump start.

temp := TempSensor newChild.  temp initialize.
flow := FlowSensor newChild.  flow initialize.
vib  := VibSensor newChild.   vib initialize.

"Wrap each sensor as an actor — Controller can dispatch concurrently"
ctrl := Controller newChild.
ctrl initWith: pump
     temp: temp asActor
     flow: flow asActor
     vib:  vib asActor.

"Run 3 cycles. Sensors run on different workers in parallel."
ctrl cycle.
ctrl cycle.
ctrl cycle.

"Final return value: number of completed cycles"
ctrl cycles.
```

Read it top to bottom. The pump is created (`newChild`), `initialize`d — the
explicit `initialize` send, because `new`/`newChild` does not do it for you —
and `start`ed. The three sensors are created and initialised the same way.

Then the key line: the controller is initialised with the pump *and with each
sensor wrapped by `asActor`*. The pump goes in as-is — a plain object — but
`temp asActor`, `flow asActor`, `vib asActor` promote the three sensors to
actors. *That* is what makes `cycle`'s fan-out parallel: because the controller
holds actor proxies, `temp read` inside `cycle` is an asynchronous send
returning a future.

Three `cycle`s are run. The script's last statement, `ctrl cycles`, is the
program's value — the number of completed cycles, `3`.

## 13.6 Running it — and measuring the speedup

Run the twin and time it:

```bash
$ time ./build/protost examples/pump_twin.st
3

real	0m0,223s
```

About 223ms — three cycles of ~50ms each (plus the pump's 30ms `start` and
runtime startup). Now force the runtime to a single worker thread, so the
sensor reads *cannot* run in parallel, and time it again:

```bash
$ time PROTOST_WORKERS=1 ./build/protost examples/pump_twin.st
3

real	0m0,506s
```

Same program, same result `3`, but ~506ms — more than twice as long. With one
worker the three sensor reads in each cycle are forced to run one after
another: 150ms per cycle instead of 50ms.

That difference — 223ms versus 506ms — is real parallelism across operating-
system threads. You wrote no threads, no locks, no `async` keywords. You wrote
plain objects, promoted three of them with `asActor`, and used the
fan-out/join pattern in one method. The runtime did the rest.

## 13.7 Why this is the digital-twin pattern

Step back and look at the shape of the program, because it generalises.

Each physical component became its own object: a pump, three sensors. The ones
that need concurrent, serialised access became actors. Each actor has its own
private state (`reading`, `state`), its own mailbox (messages processed one at
a time), and is scheduled independently by the runtime.

The controller is a plain object that **fans out** to its component actors. It
is not one big state machine — it is a small coordinator over many small
state-holders, each reacting to messages on its own.

This is why protoST's bet is "an actor *is* a digital twin". To model a second
pump, you add another `Pump` and three more sensors — more actors, no new
machinery. To model a fleet of a thousand pumps, you create a thousand actor
swarms; the cooperative scheduler ([Chapter 10](10-actors-and-futures.md))
runs them on a small worker pool because a mostly-waiting actor costs almost
nothing. To add an alarm, a logger, a persistence layer — each is another
actor. The program scales by *adding actors*, and the concurrency model never
changes.

A real industrial twin would replace the `sleep`-and-increment `read` methods
with code that talks to actual hardware — a fieldbus, an MQTT topic, a sensor
API — and would add alarm logic, history, and a dashboard. But the *skeleton*
would be exactly `pump_twin.st`: components as objects, the concurrent ones as
actors, a controller that fans out and joins. You have, in this one example,
the pattern the whole language was designed around.

## 13.8 Exercises

To make the pattern yours, try extending `pump_twin.st`:

1. **Add a pressure sensor.** Declare a `PressureSensor` like the others, wrap
   it as an actor, and have `cycle` read it too. Note that you change four
   lines and add a class — and nothing about the concurrency model.
2. **Make `cycle` `tick` the pump.** Have `cycle` send `pump tick` so the pump
   counts cycles. The pump is a plain object, so this is a synchronous send —
   contrast it with the asynchronous sensor reads.
3. **Detect an alarm.** Have `cycle` compare the aggregated reading against a
   threshold and answer a symbol — `#normal` or `#alarm` — instead of the raw
   sum. Use an `ifTrue:ifFalse:` *expression* (the robust form from
   [Chapter 7](07-exceptions.md)).
4. **Measure it yourself.** Wrap the three `ctrl cycle` calls in
   `Time millisecondsToRun:` ([Chapter 9](09-standard-library.md)) and print
   the elapsed time. Then run again under `PROTOST_WORKERS=1` and compare.

## 13.9 Summary

- `examples/pump_twin.st` is a complete digital twin: a pump (a plain-object
  FSM), three sensors, and a controller.
- Components are ordinary objects; the ones needing concurrent serialised
  access are promoted with `asActor`.
- The controller's `cycle` method is the **fan-out / join** pattern: fire all
  the sensor reads (collecting futures), *then* `wait` on the futures. That is
  what makes the three 50ms reads run in ~50ms total instead of 150ms.
- Measured: ~223ms parallel versus ~506ms forced-serial
  (`PROTOST_WORKERS=1`) — real multi-core parallelism, with no threads or locks
  in the source.
- The program scales by *adding actors* — which is exactly the digital-twin
  pattern protoST was built for.

---

Next: [Chapter 14 — For the Smalltalk programmer](14-for-the-smalltalk-programmer.md)
