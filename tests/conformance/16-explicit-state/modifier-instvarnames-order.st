"EXPECT: #('number' 'owner' 'history' 'balance')"
"Spec §6.1, conformance 80: instVarNames answers the names in source order,
 clause by clause as written and in order within each clause, whatever the
 modifier."
Object subclass: #Account
  constantNames: 'number owner'
  instanceVariableNames: 'history'
  atomicNames: 'balance'.

Account instVarNames printString
