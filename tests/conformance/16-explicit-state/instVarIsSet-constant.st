"EXPECT: #(false false true true true true 'no error')"
"Spec §6.8, conformance 100: on a constant, instVarIsSet: answers false
 after basicNew, true after a reflective fill and true on an object from the
 generated constructor. It never signals (no UnsetConstantError, no M15)."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.

[| a b |
 a := Account basicNew.
 b := Account number: 7 owner: 'ada'.
 {a instVarIsSet: #number.
  a instVarIsSet: #owner.
  (a instVarNamed: #owner put: 'bob'; instVarIsSet: #owner).
  a instVarIsSet: 'owner'.
  b instVarIsSet: #number.
  b instVarIsSet: #owner.
  'no error'}]
  on: Error do: [:e | e class name , ': ' , e messageText]
