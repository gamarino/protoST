"EXPECT: #(true 7 'ada')"
"Spec §6.2, conformance 59: basicNew is the raw allocator: it skips
 initialize and the end-of-construction check. A loader then fills the
 constants by reflection, and the readers answer them."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner

| a |
a := Account basicNew.
a instVarNamed: #number put: 7.
a instVarNamed: #owner put: 'ada'.
{a class == Account. a number. a owner}
