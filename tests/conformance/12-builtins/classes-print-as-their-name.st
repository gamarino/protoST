"EXPECT: SmallInteger Integer Float LargeInteger Array OrderedCollection Character Boolean Interval String Set Fraction Error Pt #(1 SmallInteger) Pt(1, 2)"
"A class prints as its name, whatever instance-side printOn: / printString its
 instances use (including a user printOn:)."
Object subclass: #Pt instanceVariableNames: 'x y'.
Pt >> setX: ax y: ay  x := ax. y := ay.
Pt >> printOn: aStream  aStream nextPutAll: 'Pt('; print: x; nextPutAll: ', '; print: y; nextPut: $).

| out |
out := WriteStream on: String new.
{ 3 class. Integer. 3.5 class. (2 raisedTo: 100) class. Array. OrderedCollection.
  Character. Boolean. Interval. String. Set. Fraction. Error. Pt }
  do: [:c | out print: c] separatedBy: [out space].
out space; print: {1. SmallInteger}; space; print: (Pt new setX: 1 y: 2).
out contents
