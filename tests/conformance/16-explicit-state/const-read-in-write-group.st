"EXPECT: 1"
"Plan review focus 5 and D-g: a constant read inside a run of assignments can
 signal, so it ends the run. The assignment before it is published before the
 read signals, and the handler sees it on the receiver."
Object subclass: #Account instanceVariableNames: 'history history2' constantNames: 'number owner'.
Account >> f
  history := 1. history2 := owner. ^ self

| a |
a := Account basicNew.
[a f] on: UnsetConstantError do: [:e | e receiver instVarNamed: 'history']
