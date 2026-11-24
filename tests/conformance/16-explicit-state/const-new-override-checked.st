"EXPECT: #('the constant ''owner'' of an Account is not set at the end of construction (new)' 'not reached')"
"Spec §6.2, M13, conformance 58: a class-side new override that calls super
 new gets the check. It runs in super new, so the override never goes on
 past it."
Object subclass: #Account instanceVariableNames: '' classVariableNames: 'Trace' constantNames: 'number owner'.
Account class >> trace
  ^ Trace
Account class >> new
  | a |
  Trace := 'not reached'.
  a := super new.
  Trace := 'reached'.
  ^ a
Account >> initialize
  number := 1

{[Account new] on: Error do: [:e | e messageText]. Account trace}
