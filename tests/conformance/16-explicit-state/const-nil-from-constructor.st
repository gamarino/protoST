"EXPECT: #('the constant ''owner'' of an Account is not set at the end of construction (number:owner:)' true)"
"Spec §6.2, M13, conformance 56: the generated constructor never writes nil,
 so a nil argument leaves its constant unset and the check after initialize
 names it and the constructor. M13 is an Error, signalled in place."
Object subclass: #Account instanceVariableNames: 'seen' constantNames: 'number owner'.
Account >> initialize
  seen := number

| err |
err := [Account number: 7 owner: nil. nil] on: Error do: [:e | e].
{err messageText. err class == Error}
