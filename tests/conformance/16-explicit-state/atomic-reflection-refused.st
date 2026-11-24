"EXPECT: #('instVarNamed:put: cannot write the atomic variable ''balance'' of an Account: use update:with: or compareAndSet:from:to:' 'instVarNamed:put: cannot write the atomic variable ''balance'' of an Account: use update:with: or compareAndSet:from:to:' nil true 'instVarNamed:put: cannot write the atomic variable ''balance'' of an Account: use update:with: or compareAndSet:from:to:' 3)"
"Spec §6.5 and §11, M6, conformance 74: instVarNamed:put: and instVarAt:put:
 never write an atomic variable, set or unset: they signal
 ModificationForbidden (M6) and nothing is written. compareAndSet:from:to:
 still writes it, and instVarNamed: still reads it."
Object subclass: #Account instanceVariableNames: '' atomicNames: 'balance'.

| a refused r |
refused := [:block | block on: ModificationForbidden do: [:e | e messageText]].
a := Account basicNew.
r := OrderedCollection new.
r add: (refused value: [a instVarNamed: #balance put: 1]).
r add: (refused value: [a instVarAt: 1 put: 1]).
r add: (a instVarNamed: #balance).
r add: (a compareAndSet: #balance from: nil to: 3).
r add: (refused value: [a instVarNamed: 'balance' put: 4]).
r add: (a instVarNamed: #balance).
r asArray
