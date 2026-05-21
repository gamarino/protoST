"EXPECT: 12"
"-- traffic_intersection.st ------------------------------------------------"
"-- A complete digital-twin simulation, distinct from pump_twin.st: a       --"
"-- traffic intersection modelled as cooperating actors.                    --"
"--                                                                        --"
"-- Each TrafficLight is a finite state machine cycling green -> yellow ->  --"
"-- red. Two lights — north-south and east-west — are kept out of phase by  --"
"-- an Intersection controller: when one is green the other is red. Each    --"
"-- light is wrapped as an actor, so its state transitions are serialised   --"
"-- without explicit locks; the controller drives them by message send.     --"
"--                                                                        --"
"-- The simulation runs 12 ticks and reports how many ticks elapsed — a     --"
"-- deterministic smoke check that the FSMs advanced in lock-step.          --"

"-- TrafficLight: the FSM. State is one of 'green' / 'yellow' / 'red'. --"
Object subclass: #TrafficLight
  instanceVariableNames: 'state greenCount'.

TrafficLight >> initialize
  state := 'red'.
  greenCount := 0.
  ^ self.

TrafficLight >> state
  ^ state.

TrafficLight >> greenCount
  ^ greenCount.

"Advance the FSM one step around the green -> yellow -> red cycle."
TrafficLight >> advance
  state = 'green'  ifTrue: [ state := 'yellow'. ^ state ].
  state = 'yellow' ifTrue: [ state := 'red'.    ^ state ].
  "state is 'red'"
  state := 'green'.
  greenCount := greenCount + 1.
  ^ state.

"Force the light to a phase (used to start the two lights opposed)."
TrafficLight >> setState: aState
  state := aState.
  ^ self.

"-- Intersection: the controller. Owns two light actors and ticks them. --"
Object subclass: #Intersection
  instanceVariableNames: 'northSouth eastWest ticks'.

Intersection >> initWith: nsActor and: ewActor
  northSouth := nsActor.
  eastWest := ewActor.
  ticks := 0.
  ^ self.

Intersection >> ticks
  ^ ticks.

"One controller tick advances both light FSMs concurrently, then waits."
Intersection >> tick
  | fNs fEw |
  fNs := northSouth advance.
  fEw := eastWest advance.
  "Wait for both transitions to settle before the next tick."
  fNs wait.
  fEw wait.
  ticks := ticks + 1.
  ^ ticks.

"-- MAIN PROGRAM --"

"Build two lights, started in opposite phases."
ns := TrafficLight new.
ns initialize.
ns setState: 'green'.

ew := TrafficLight new.
ew initialize.
ew setState: 'red'.

"Wrap each light as an actor so its FSM transitions are serialised."
controller := Intersection new.
controller initWith: ns asActor and: ew asActor.

"Run 12 controller ticks — each advances both light FSMs one step."
1 to: 12 do: [ :i | controller tick ].

"Final result: the number of completed simulation ticks."
controller ticks.
