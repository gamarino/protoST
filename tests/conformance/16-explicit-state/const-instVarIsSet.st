"EXPECT: #(false true 7 'no error')"
"Spec §6.8, conformance 92: instVarIsSet: answers false on an unset
 constant and true once a reflective fill has set it. It never reads the
 constant as a value, so it never signals UnsetConstantError while the
 constant is unset: the protected block answers the readings, not a
 handler's text."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number

[| a before after |
 a := Account basicNew.
 before := a instVarIsSet: #number.
 a instVarNamed: #number put: 7.
 after := a instVarIsSet: 'number'.
 {before. after. a number. 'no error'}]
  on: Error do: [:e | e class name , ': ' , e messageText]
