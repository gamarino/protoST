"EXPECT: #('cannot assign to the constant ''n'' of Acc outside initialize (line 4)' 'cannot assign to the constant ''n'' of Acc inside a block: the block may run after construction (line 4)' 'cannot assign to the constant ''n'' of Acc inside a block: the block may run after construction (line 5)' 'cannot assign to the atomic variable ''b'' of Acc: use update:with: or compareAndSet:from:to: (line 4)' 5 3)"
"Plan review focus 1: a class built by the message form (an expression
 receiver, pre-flight ruling C4) is not visible to the compiler, so its
 methods are resolved when they are installed. They get the compiler's
 rules there: M1, M2 (also for a real block inside an inlined one) and M4 refuse the install (each refused method is in a
 helper module, so the refusal is caught here); initialize stores the
 constant; a write-once store installs as STORE_ONCE_INSTVAR (rulings C2,
 C3)."
(Object yourself) subclass: #Acc
  instanceVariableNames: 'h'
  constantNames: 'n'
  writeOnceNames: 'w'
  atomicNames: 'b'.
Acc >> initialize
  n := 5
Acc >> setW: x
  w := x

| outside inBlock inInlined atomic a |
outside := [Import from: '_late_rules_constant_outside'. 'installed'] on: Error do: [:e | e messageText].
inBlock := [Import from: '_late_rules_constant_in_block'. 'installed'] on: Error do: [:e | e messageText].
inInlined := [Import from: '_late_rules_closure_in_inlined'. 'installed'] on: Error do: [:e | e messageText].
atomic := [Import from: '_late_rules_atomic'. 'installed'] on: Error do: [:e | e messageText].
a := Acc new.
a setW: 3.
{outside. inBlock. inInlined. atomic. a instVarNamed: #n. a instVarNamed: #w}
