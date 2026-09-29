"EXPECT: Point<3,4> Point<3,4>"
"A user printOn: is used by printString and displayString."
Object subclass: #MyPoint instanceVariableNames: 'x y'.
MyPoint >> setX: ax y: ay
  x := ax.
  y := ay.
  ^ self.

MyPoint >> printOn: aStream
  aStream nextPutAll: 'Point<'; print: x; nextPutAll: ','; print: y; nextPutAll: '>'.

p := MyPoint new setX: 3 y: 4.
p printString , ' ' , p displayString.
