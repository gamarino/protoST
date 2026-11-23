"EXPECT: #('cannot assign to the constant ''n'' of Acc outside initialize (line 4)' 'cannot assign to the constant ''n'' of Acc inside a block: the block may run after construction (line 4)' 'cannot assign to the atomic variable ''b'' of Acc: use update:with: or compareAndSet:from:to: (line 4)' 5 3)"
"Plan review focus 1: a class built by the message form (an expression
 receiver, pre-flight ruling C4) is not visible to the compiler, so its
 methods are resolved when they are installed. They get the compiler's
 rules there: M1, M2 and M4 refuse the install (each refused method is in a
 helper module, so the refusal is caught here); initialize stores the
 constant; a write-once store installs (until Task 4 as a plain store,
 ruling C3)."
(Object yourself) subclass: #Acc
  instanceVariableNames: 'h'
  constantNames: 'n'
  writeOnceNames: 'w'
  atomicNames: 'b'.
Acc >> initialize
  n := 5
Acc >> setW: x
  w := x

| outside inBlock atomic a |
outside := [Import from: '_late_rules_constant_outside'. 'installed'] on: Error do: [:e | e messageText].
inBlock := [Import from: '_late_rules_constant_in_block'. 'installed'] on: Error do: [:e | e messageText].
atomic := [Import from: '_late_rules_atomic'. 'installed'] on: Error do: [:e | e messageText].
a := Acc new.
a setW: 3.
{outside. inBlock. atomic. a instVarNamed: #n. a instVarNamed: #w}
