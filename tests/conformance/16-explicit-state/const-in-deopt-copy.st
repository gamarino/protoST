"EXPECT: 1"
"Plan review focus 2: a constant assigned in an inlined block of initialize
 whose receiver is not a Boolean. The send is then made for real, with the
 de-optimised copy of the block, and Truthy>>ifTrue: evaluates it: the copy
 stores the constant like the inlined code, with no compile error."
Object subclass: #Truthy.
Truthy >> ifTrue: aBlock
  ^ aBlock value

Object subclass: #Account instanceVariableNames: 'flag' constantNames: 'number'.
Account >> initialize
  flag := Truthy new.
  flag ifTrue: [number := 1]
Account >> number
  ^ number

Account new number
