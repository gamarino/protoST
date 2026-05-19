"-- counter.st --"
Object subclass: #Counter
  instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.

"-- top-level expression --"
(Counter startingAt: 10) increment.
