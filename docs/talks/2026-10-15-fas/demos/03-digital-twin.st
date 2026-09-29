"Demo 3 - a digital twin: a pump is a state machine living in an actor,
 three sensors are actors with 50 ms of simulated I/O each, and a
 controller reads them concurrently every cycle and stops the pump when
 the temperature crosses a limit. One message at a time per actor: no
 locks anywhere."

Object subclass: #Pump instanceVariableNames: 'state cycles'.

Pump >> initialize
  state := #off.
  cycles := 0

Pump >> start
  state := #running.
  ^ state

Pump >> alarm: reason
  state := #stopped.
  ^ state

Pump >> tick
  state = #running ifTrue: [cycles := cycles + 1].
  ^ state

Pump >> cycles
  ^ cycles

Object subclass: #Sensor instanceVariableNames: 'value step'.

Sensor class >> from: start step: delta
  ^ self new setValue: start step: delta

Sensor >> setValue: start step: delta
  value := start.
  step := delta

Sensor >> read
  Object sleep: 50.
  value := value + step.
  ^ value

Object subclass: #Controller instanceVariableNames: 'pump temp flow vib limit'.

Controller >> pump: p temp: t flow: f vib: v limit: celsius
  pump := p. temp := t. flow := f. vib := v. limit := celsius

Controller >> cycle: n
  | readings state |
  readings := (Future whenAll: {temp read. flow read. vib read}) wait.
  state := pump tick wait.
  Transcript showCr: 'cycle ' , n printString , '  temp ' , (readings at: 1) printString
    , '  flow ' , (readings at: 2) printString , '  vib ' , (readings at: 3) printString
    , '  ' , state.
  (readings at: 1) > limit ifTrue: [
    Transcript showCr: '  temperature above ' , limit printString , ': ' , (pump alarm: #overheat) wait].
  ^ state

| pump controller ms |
pump := Pump new asActor.
controller := Controller new
  pump: pump
  temp: (Sensor from: 60 step: 4) asActor
  flow: (Sensor from: 0 step: 12) asActor
  vib: (Sensor from: 0 step: 1) asActor
  limit: 75.
pump start wait.
ms := Time millisecondsToRun: [1 to: 5 do: [:n | controller cycle: n]].
Transcript showCr: 'time per cycle: ' , (ms // 5) printString , ' ms (three 50 ms reads each)'.
Transcript showCr: pump cycles wait printString.
