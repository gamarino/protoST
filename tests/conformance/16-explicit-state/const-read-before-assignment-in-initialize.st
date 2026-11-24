"EXPECT: #('constant ''number'' of an Account read before it was set' 2 6)"
"Spec §6.2, M14, conformance 90: initialize is code like any other. Reading a
 constant before initialize assigned it (`number isNil ifTrue: [...]`)
 signals UnsetConstantError at the read; asking whether it is set is
 instVarIsSet:'s job. A constant that initialize assigned first reads
 freely there, and so does one the generated constructor set: it is
 written before initialize runs."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> initialize
  number isNil ifTrue: [number := 1].
  owner := 'x'
Object subclass: #Ledger instanceVariableNames: 'seen' constantNames: 'number'.
Ledger >> initialize
  number := 1.
  seen := number + 1
Ledger >> seen
  ^ seen
Object subclass: #Given instanceVariableNames: 'seen' constantNames: 'number'.
Given >> initialize
  seen := number + 1
Given >> seen
  ^ seen

{[Account basicNew initialize. 'no error'] on: UnsetConstantError do: [:e | e messageText].
 Ledger new seen.
 (Given number: 5) seen}
