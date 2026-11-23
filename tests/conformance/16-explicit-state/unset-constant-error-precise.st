"EXPECT: #('inner' 'outer Error' 'outer MessageNotUnderstood' 'outer ModificationForbidden')"
"Spec §6.9, conformance 95: an on: UnsetConstantError handler nested inside an
 on: Error handler catches the unset-constant read only. Error signal:, a
 MessageNotUnderstood and a ModificationForbidden signalled in the same
 protected block pass through to the outer handler. Each answer names the
 handler that caught it, and the outer one also the class it caught."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> owner
  ^ owner

| caught |
caught := [:block |
  [[block value] on: UnsetConstantError do: [:e | 'inner']]
    on: Error do: [:e | 'outer ' , e class name]].
{caught value: [Account basicNew owner].
 caught value: [Error signal: 'other'].
 caught value: [3 zork].
 caught value: ['abc' copy at: 1 put: $x]}
