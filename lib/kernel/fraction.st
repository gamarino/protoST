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
  "Exact, as in Pharo: (1/10) = 0.1 is false, (1/2) = 0.5 is true."
  aNumber isFloat ifTrue: [^ aNumber isFinite and: [self = aNumber asExactFraction]].
  aNumber isInteger ifTrue: [^ false].
  ^ numerator = aNumber numerator and: [denominator = aNumber denominator].

Fraction >> hash
  "Equal to the hash of the Float it equals, when there is one."
  | f |
  f := self asFloat.
  (f isFinite and: [f asExactFraction = self]) ifTrue: [^ f hash].
  ^ numerator hash bitXor: denominator hash.

Fraction >> truncated
  ^ numerator quo: denominator.

Fraction >> floor
  ^ numerator // denominator.

Fraction >> ceiling
  ^ (numerator + denominator - 1) // denominator.

Fraction >> rounded
  "Halves round away from zero, as in Pharo: (-1/2) rounded = -1."
  ^ (self + (self sign / 2)) truncated.

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
  "Comparisons with a Float are exact; arithmetic with a Float is Float."
  (rcvr isFloat and: [#(#= #~= #< #> #<= #>=) includes: selector])
    ifTrue: [^ rcvr isFinite
      ifTrue: [rcvr asExactFraction perform: selector with: self]
      ifFalse: [rcvr asFloat perform: selector with: self asFloat]].
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

"--- the rest of the Number protocol for Fractions ----------------------------
 Fraction inherits the native number primitives through Number; each one that
 would read the Fraction as a native number is answered here instead: exactly
 where the result is rational, through asFloat where it is not."

Fraction >> ~= aNumber
  ^ (self = aNumber) not.

Fraction >> quo: aNumber
  ^ (self / aNumber) truncated.

Fraction >> rem: aNumber
  ^ self - ((self quo: aNumber) * aNumber).

Fraction >> isZero
  ^ numerator = 0.

Fraction >> sign
  ^ numerator sign.

Fraction >> squared
  ^ self * self.

Fraction >> even
  ^ false.

Fraction >> odd
  ^ false.

Fraction >> isEven
  ^ false.

Fraction >> isOdd
  ^ false.

Fraction >> asInteger
  ^ self truncated.

Fraction >> raisedTo: exponent
  exponent isInteger ifFalse: [^ self asFloat raisedTo: exponent asFloat].
  exponent < 0 ifTrue: [^ (self raisedTo: exponent negated) reciprocal].
  ^ (numerator raisedTo: exponent) / (denominator raisedTo: exponent).

Fraction >> sqrt
  ^ self asFloat sqrt.

Fraction >> sin
  ^ self asFloat sin.

Fraction >> cos
  ^ self asFloat cos.

Fraction >> tan
  ^ self asFloat tan.

Fraction >> arcSin
  ^ self asFloat arcSin.

Fraction >> arcCos
  ^ self asFloat arcCos.

Fraction >> arcTan
  ^ self asFloat arcTan.

Fraction >> ln
  ^ self asFloat ln.

Fraction >> exp
  ^ self asFloat exp.

Fraction >> log
  ^ self asFloat log.

Fraction >> log: aNumber
  ^ self asFloat log: aNumber.

Fraction >> factorial
  ^ self error: 'factorial is defined for non-negative integers only'.

Fraction >> gcd: aNumber
  ^ self error: 'gcd: is defined for integers only'.

Fraction >> lcm: aNumber
  ^ self error: 'lcm: is defined for integers only'.

"--- exact conversion of Floats ------------------------------------------------"

"The exact value of a finite Float: an Integer or a Fraction whose
 denominator is a power of two (Pharo's asExactFraction)."
Number >> asExactFraction
  | parts mantissa exponent |
  self isFloat ifFalse: [^ self].
  self isFinite ifFalse: [^ self error: 'cannot convert a non-finite Float to a Fraction'].
  parts := self __floatParts.
  mantissa := parts at: 1.
  exponent := parts at: 2.
  exponent >= 0 ifTrue: [^ mantissa * (2 raisedTo: exponent)].
  ^ mantissa / (2 raisedTo: exponent negated).

Number >> isFinite
  ^ self isFloat not or: [(self - self) = 0.0].
