"EXPECT: #(7 'ada' 7 'ada' true)"
"Spec §6.2 and §11, conformance 87: a loader makes the object with basicNew,
 fills both constants with instVarNamed:put:, and the object then reads them
 normally, through its accessors and by reflection. No error at any point:
 the protected block answers the readings, not a handler's text."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner

Object subclass: #Loader.
Loader >> load: aDictionary
  | a |
  a := Account basicNew.
  a instVarNamed: #number put: (aDictionary at: #number).
  a instVarNamed: 'owner' put: (aDictionary at: #owner).
  ^ a

| record |
record := Dictionary new.
record at: #number put: 7; at: #owner put: 'ada'.
[| a | a := Loader new load: record.
 {a number. a owner. a instVarAt: 1. a instVarNamed: #owner. a class == Account}]
  on: Error do: [:e | e messageText]
