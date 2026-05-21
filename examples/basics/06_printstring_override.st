"EXPECT: Point(3, 4)"
"-- 06_printstring_override.st ---------------------------------------------"
"-- Every object answers `printString`; a class may override it to give a   --"
"-- domain-specific rendering. Here `Point` prints as `Point(x, y)` instead --"
"-- of the default `a Point`.                                               --"
"-- Expected output: Point(3, 4)                                            --"

Object subclass: #Point instanceVariableNames: 'x y'.

Point >> setX: anX y: aY
  x := anX.
  y := aY.
  ^ self.

Point >> printString
  ^ 'Point(' , x printString , ', ' , y printString , ')'.

(Point new setX: 3 y: 4) printString.
