"EXPECT: the constant 'owner' of an Account is not set at the end of construction (new)"
"Spec §6.2, M13, conformance 55: when initialize returns inside new, every
 constant of the class must be set; the first unset one is named."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> initialize
  number := 1

[Account new] on: Error do: [:e | e messageText]
