"EXPECT: #('unset' nil 'unset' 7 7 'the constant ''number'' of an Account is already set; nothing was written' 'the constant ''number'' of an Account is already set; nothing was written' 'the constant ''number'' of an Account is already set; nothing was written' 7 'Ann' 'the constant ''owner'' of an Account is already set; nothing was written' 'Ann')"
"Spec §6.5 and §11, M3, conformance 53: instVarNamed:put: (and instVarAt:put:,
 which reaches it) fills a constant only while it is unset. Writing nil to an
 unset constant changes nothing and signals nothing. Once set, any reflective
 write signals ModificationForbidden (M3), an equal value and nil included,
 and the constant keeps its value."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner

| a refused unset r |
refused := [:block | block on: ModificationForbidden do: [:e | e messageText]].
unset := [:block | block on: UnsetConstantError do: [:e | #unset]].
a := Account basicNew.
r := OrderedCollection new.
r add: (unset value: [a number]).
r add: (a instVarNamed: #number put: nil).
r add: (unset value: [a number]).
r add: (a instVarNamed: #number put: 7).
r add: a number.
r add: (refused value: [a instVarNamed: #number put: 8]).
r add: (refused value: [a instVarNamed: 'number' put: 7]).
r add: (refused value: [a instVarNamed: #number put: nil]).
r add: a number.
r add: (a instVarAt: 2 put: 'Ann').
r add: (refused value: [a instVarAt: 2 put: 'Ann']).
r add: a owner.
r asArray
