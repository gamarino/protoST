"EXPECT: #('the constant ''number'' of a Savings is not set at the end of construction (new)' 2)"
"Spec §6.2, M13, conformance 57: the check covers inherited constants. A
 subclass initialize that does not send super initialize leaves the
 inherited number unset."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number'.
Account >> initialize
  number := 1
Account subclass: #Savings instanceVariableNames: 'rate' constantNames: 'limit'.
Savings >> initialize
  limit := 2.
  rate := 0
Savings >> limit
  ^ limit
Account subclass: #Checking instanceVariableNames: '' constantNames: 'limit'.
Checking >> initialize
  super initialize.
  limit := 2
Checking >> limit
  ^ limit

{[Savings new] on: Error do: [:e | e messageText]. Checking new limit}
