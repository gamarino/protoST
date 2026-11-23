"EXPECT: #('constant ''owner'' of an Account read before it was set' 'constant ''owner'' of an Account read before it was set' 'Ann' 'Ann' 7 7 nil)"
"Spec §6.2 and §11, M14, conformance 91: instVarAt: and instVarNamed: read
 what a program reads, so on an unset constant they signal
 UnsetConstantError; on a set one they answer it. A plain variable that is
 nil still answers nil."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Object subclass: #Note instanceVariableNames: 'text'.

| a b unset |
unset := [:block | block on: UnsetConstantError do: [:e | e messageText]].
a := Account basicNew.
a instVarNamed: 'number' put: 7.
b := Account basicNew.
b instVarNamed: 'owner' put: 'Ann'.
{unset value: [a instVarAt: 2].
 unset value: [a instVarNamed: 'owner'].
 b instVarAt: 2.
 b instVarNamed: #owner.
 a instVarAt: 1.
 a instVarNamed: 'number'.
 Note basicNew instVarNamed: 'text'}
