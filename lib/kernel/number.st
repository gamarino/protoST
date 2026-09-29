"Kernel: Number, Integer and BlockClosure protocol written in protoST."

Number >> timesRepeat: aBlock
  | i |
  i := 1.
  [i <= self] whileTrue: [aBlock value. i := i + 1].

Number >> isNumber
  ^ true.

Number >> negative
  ^ self < 0.

Number >> positive
  ^ self >= 0.

Number >> strictlyPositive
  ^ self > 0.

Number >> asString
  ^ self printString.

Number >> to: stop collect: aBlock
  | out |
  out := OrderedCollection new.
  self to: stop do: [:i | out add: (aBlock value: i)].
  ^ out asArray.

Integer >> printString: base
  | n digits |
  base < 2 | (base > 36) ifTrue: [^ self error: 'base must be between 2 and 36'].
  self = 0 ifTrue: [^ '0'].
  n := self abs.
  digits := ''.
  [n > 0] whileTrue: [
    digits := ('0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ' at: n \\ base + 1) asString , digits.
    n := n // base].
  ^ self < 0 ifTrue: ['-' , digits] ifFalse: [digits].

Integer >> radix: base
  ^ self printString: base.

Integer >> printStringRadix: base
  ^ base printString , 'r' , (self printString: base).

Integer >> printPaddedWith: aCharacter to: width
  | s |
  s := self printString.
  [s size < width] whileTrue: [s := aCharacter asString , s].
  ^ s.

Integer >> isPrime
  | i |
  self < 2 ifTrue: [^ false].
  self < 4 ifTrue: [^ true].
  self even ifTrue: [^ false].
  i := 3.
  [i * i <= self] whileTrue: [
    self \\ i = 0 ifTrue: [^ false].
    i := i + 2].
  ^ true.

BlockClosure >> isBlock
  ^ true.

BlockClosure >> cull: anObject
  ^ self numArgs = 0 ifTrue: [self value] ifFalse: [self value: anObject].

BlockClosure >> cull: first cull: second
  self numArgs = 0 ifTrue: [^ self value].
  self numArgs = 1 ifTrue: [^ self value: first].
  ^ self value: first value: second.

"--- magnitude protocol --------------------------------------------------------
 Built on the comparison operators, which dispatch across the numeric tower
 (native numbers and Fractions, in either position), as Magnitude does in
 Smalltalk-80. The tie and NaN rules follow Magnitude: min: answers the
 receiver only when it is strictly smaller, max: only when strictly larger."

Number >> min: aNumber
  ^ self < aNumber ifTrue: [self] ifFalse: [aNumber].

Number >> max: aNumber
  ^ self > aNumber ifTrue: [self] ifFalse: [aNumber].

Number >> between: min and: max
  ^ min <= self and: [self <= max].

Number >> ** exponent
  ^ self raisedTo: exponent.

Number >> roundTo: quantum
  ^ (self / quantum) rounded * quantum.

Number >> truncateTo: quantum
  ^ (self quo: quantum) * quantum.

"--- integers --------------------------------------------------------------------"

Integer >> reciprocal
  ^ 1 / self.

Integer >> << shift
  ^ self bitShift: shift.

Integer >> bitAt: index
  ^ (self bitShift: 1 - index) bitAnd: 1.

Integer >> isPowerOfTwo
  ^ self > 0 and: [(self bitAnd: self - 1) = 0].
