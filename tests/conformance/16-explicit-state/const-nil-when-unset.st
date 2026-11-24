"EXPECT: #(3 'the constant ''number'' of a Ledger is not set at the end of construction (new)')"
"Spec §6.2, conformance 52: storing nil into an unset constant changes
 nothing, so a later store sets it. Assigning only nil leaves the constant
 unset, and new then signals M13 for it: nil counts as unset."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> initialize
  number := nil.
  number := 3.
  owner := 'x'
Account >> number
  ^ number
Object subclass: #Ledger instanceVariableNames: '' constantNames: 'number'.
Ledger >> initialize
  number := nil

{Account new number. [Ledger new] on: Error do: [:e | e messageText]}
