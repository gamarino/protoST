"Kernel: Fraction. Integer / Integer answers an exact, reduced Fraction when
 the division is not exact; mixed arithmetic with Integers and Floats works in
 both directions (the native primitives ask a Fraction argument to adapt the
 receiver: adaptToNumber:andSend:)."

Number subclass: #Fraction instanceVariableNames: 'numerator denominator'.

Fraction class >> numerator: n denominator: d
  | g num den |
  d = 0 ifTrue: [^ ZeroDivide new signal: 'ZeroDivide'].
  g := n gcd: d.
  num := n // g.
  den := d // g.
  den < 0 ifTrue: [num := num negated. den := den negated].
  den = 1 ifTrue: [^ num].
  ^ self basicNew setNumerator: num denominator: den.

Fraction >> setNumerator: n denominator: d
  numerator := n.
  denominator := d.
  ^ self.

Fraction >> numerator
  ^ numerator.

Fraction >> denominator
  ^ denominator.

Fraction >> isFraction
  ^ true.

Fraction >> asFloat
  ^ numerator asFloat / denominator asFloat.

Fraction >> asFraction
  ^ self.

Fraction >> + aNumber
  aNumber isFloat ifTrue: [^ self asFloat + aNumber].
  aNumber isInteger ifTrue: [^ Fraction numerator: numerator + (aNumber * denominator) denominator: denominator].
  ^ Fraction numerator: (numerator * aNumber denominator) + (aNumber numerator * denominator)
      denominator: denominator * aNumber denominator.

Fraction >> - aNumber
  ^ self + aNumber negated.

Fraction >> * aNumber
  aNumber isFloat ifTrue: [^ self asFloat * aNumber].
  aNumber isInteger ifTrue: [^ Fraction numerator: numerator * aNumber denominator: denominator].
  ^ Fraction numerator: numerator * aNumber numerator denominator: denominator * aNumber denominator.

Fraction >> / aNumber
  aNumber isFloat ifTrue: [^ self asFloat / aNumber].
  aNumber isInteger ifTrue: [^ Fraction numerator: numerator denominator: denominator * aNumber].
  ^ Fraction numerator: numerator * aNumber denominator denominator: denominator * aNumber numerator.

Fraction >> // aNumber
  ^ (self / aNumber) floor.

Fraction >> \\ aNumber
  ^ self - ((self // aNumber) * aNumber).

Fraction >> negated
  ^ Fraction numerator: numerator negated denominator: denominator.

Fraction >> reciprocal
  ^ Fraction numerator: denominator denominator: numerator.

Fraction >> abs
  ^ numerator < 0 ifTrue: [self negated] ifFalse: [self].

Fraction >> < aNumber
  aNumber isFloat ifTrue: [^ self asFloat < aNumber].
  ^ (self - aNumber) numerator < 0.

Fraction >> > aNumber
  ^ aNumber < self.

Fraction >> <= aNumber
  ^ (self > aNumber) not.

Fraction >> >= aNumber
  ^ (self < aNumber) not.

Fraction >> = aNumber
  aNumber isNumber ifFalse: [^ false].
  aNumber isFloat ifTrue: [^ self asFloat = aNumber].
  aNumber isInteger ifTrue: [^ false].
  ^ numerator = aNumber numerator and: [denominator = aNumber denominator].

Fraction >> hash
  ^ self asFloat hash.

Fraction >> truncated
  ^ numerator quo: denominator.

Fraction >> floor
  ^ numerator // denominator.

Fraction >> ceiling
  ^ (numerator + denominator - 1) // denominator.

Fraction >> rounded
  ^ (self + (1/2)) floor.

Fraction >> printOn: aStream
  aStream nextPut: $(; print: numerator; nextPut: $/; print: denominator; nextPut: $).

"Number has a native printString for native numbers; a Fraction prints
 through printOn: like any other object."
Fraction >> printString
  self isClassObject ifTrue: [^ self name].
  ^ String streamContents: [:s | self printOn: s].

Fraction >> displayString
  self isClassObject ifTrue: [^ self name].
  ^ numerator printString , '/' , denominator printString.

"Double dispatch from the native primitives: a native number op aFraction."
Fraction >> adaptToNumber: rcvr andSend: selector
  rcvr isFloat ifTrue: [^ rcvr perform: selector with: self asFloat].
  ^ (Fraction basicNew setNumerator: rcvr denominator: 1) perform: selector with: self.

Integer >> / aNumber
  aNumber isInteger ifTrue: [^ Fraction numerator: self denominator: aNumber].
  aNumber isFloat ifTrue: [^ self asFloat / aNumber].
  ^ (Fraction basicNew setNumerator: self denominator: 1) / aNumber.

Integer >> numerator
  ^ self.

Integer >> denominator
  ^ 1.

Number >> isFraction
  ^ false.

Integer >> asFraction
  ^ self.
