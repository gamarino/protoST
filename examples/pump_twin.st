"-- pump_twin.st: digital-twin demo for protoST --"
"--                                                                  --"
"--   A simulated industrial pump with 3 sensors (temperature, flow, --"
"--   vibration) orchestrated by a Controller. Each sensor read has  --"
"--   simulated 50ms I/O latency.                                    --"
"--                                                                  --"
"--   When sensors are wrapped as actors, the Controller's `cycle`   --"
"--   message fires the 3 reads concurrently and waits for all 3.    --"
"--   Wall-clock per cycle is ~50ms (parallel) vs ~150ms (serial).   --"
"--                                                                  --"
"--   This is the core 'digital-twin' pattern: each physical         --"
"--   component is its own actor with its own state and message      --"
"--   handler, scheduled independently by the runtime.               --"

"-- Pump: an FSM that toggles between 'off' and 'running'"
Object subclass: #Pump instanceVariableNames: 'state ticks'.

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

Pump >> state ^ state.
Pump >> ticks ^ ticks.

"-- Three independent sensors, each with simulated I/O latency"
Object subclass: #TempSensor instanceVariableNames: 'reading'.
TempSensor >> initialize reading := 20.
TempSensor >> read
  Object sleep: 50.
  reading := reading + 3.
  ^ reading.

Object subclass: #FlowSensor instanceVariableNames: 'reading'.
FlowSensor >> initialize reading := 0.
FlowSensor >> read
  Object sleep: 50.
  reading := reading + 7.
  ^ reading.

Object subclass: #VibSensor instanceVariableNames: 'reading'.
VibSensor >> initialize reading := 0.
VibSensor >> read
  Object sleep: 50.
  reading := reading + 1.
  ^ reading.

"-- Controller: owns the pump + sensors, runs cycles."
"--   Each cycle dispatches 3 reads in parallel and aggregates."
Object subclass: #Controller instanceVariableNames: 'pump temp flow vib cycles'.

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

Controller >> cycles ^ cycles.

"-- MAIN PROGRAM --"

pump := Pump newChild. pump initialize. pump start.

temp := TempSensor newChild. temp initialize.
flow := FlowSensor newChild. flow initialize.
vib  := VibSensor newChild.  vib initialize.

"Wrap each sensor as an actor — Controller can dispatch concurrently"
ctrl := Controller newChild.
ctrl initWith: pump temp: temp asActor flow: flow asActor vib: vib asActor.

"Run 3 cycles. Sensors run on different workers in parallel."
"Expected wall-clock: ~150ms (3 cycles × 50ms parallel) + startup."
"Serial baseline would be ~450ms (3 cycles × 150ms)."
ctrl cycle.
ctrl cycle.
ctrl cycle.

"Final return value: number of completed cycles"
ctrl cycles.
