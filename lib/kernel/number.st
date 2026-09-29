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

"--- SmallInteger range ------------------------------------------------------------
 A SmallInteger is a tagged 56-bit signed integer, so it spans -2^55 .. 2^55 - 1;
 arithmetic beyond that answers a LargeInteger."

SmallInteger class >> maxVal
  ^ 36028797018963967.

SmallInteger class >> minVal
  ^ -36028797018963968.

"--- integer bits and roots ---------------------------------------------------------"

Integer >> highBit
  "The index of the highest set bit of a non-negative integer (0 for 0)."
  | n bits |
  self < 0 ifTrue: [^ self error: 'highBit is not defined for negative integers'].
  n := self.
  bits := 0.
  [n > 0] whileTrue: [n := n bitShift: -1. bits := bits + 1].
  ^ bits.

Integer >> isBitSet: index
  "Whether bit index (1 = least significant) is set, as bitAt: index = 1."
  ^ (self bitAt: index) = 1.

Integer >> sqrtFloor
  "The largest integer whose square does not exceed the receiver, computed
   exactly, bit by bit, with shifts, additions and comparisons only, so it
   is correct for LargeIntegers of any size."
  | n root bit |
  self < 0 ifTrue: [^ self error: 'sqrtFloor is not defined for negative integers'].
  n := self.
  root := 0.
  bit := 1 bitShift: self highBit - 1 // 2 * 2.
  [bit > 0] whileTrue: [
    n >= (root + bit)
      ifTrue: [n := n - (root + bit). root := (root bitShift: -1) + bit]
      ifFalse: [root := root bitShift: -1].
    bit := bit bitShift: -2].
  ^ root.

Integer >> isProbablyPrime
  "isPrime is exact (trial division), so this answers the same."
  ^ self isPrime.

"--- NaN and infinities -------------------------------------------------------------
 Only a Float can be NaN or infinite."

Number >> isNaN
  ^ false.

Number >> isInfinite
  ^ false.

Float >> isNaN
  ^ self ~= self.

Float >> isInfinite
  "An infinity minus itself is NaN; a finite Float minus itself is 0."
  ^ self isNaN not and: [(self - self) isNaN].

"--- integer and fraction parts -----------------------------------------------------"

Number >> fractionPart
  "The receiver minus its integer part, with the receiver's sign:
   3.75 fractionPart = 0.75, (7/2) fractionPart = (1/2)."
  ^ self - self truncated.

Number >> integerPart
  "The receiver truncated toward zero: an Integer for an exact number."
  ^ self truncated.

Float >> integerPart
  "A Float for a Float, as in Pharo: 3.75 integerPart = 3.0."
  ^ self truncated asFloat.

"--- printing with a fixed number of decimals -------------------------------------"

Number >> printShowingDecimalPlaces: placesDesired
  "The receiver rounded to placesDesired decimals, printed with exactly that
   many digits after the point: 3.14159 printShowingDecimalPlaces: 2 = '3.14'.
   A value that rounds to zero prints without a sign."
  | scale scaled |
  (self isNaN or: [self isInfinite]) ifTrue: [^ self printString].
  placesDesired <= 0 ifTrue: [^ self rounded printString].
  scale := 10 raisedTo: placesDesired.
  scaled := (self * scale) rounded.
  ^ (scaled < 0 ifTrue: ['-'] ifFalse: ['']) , (scaled abs // scale) printString , '.' ,
      (scaled abs \\ scale printPaddedWith: $0 to: placesDesired).

"--- reading numbers ---------------------------------------------------------------"

Number class >> readFrom: aStringOrStream
  "The number written at the start of a String or ReadStream (after any
   separators), in literal syntax: an optional minus sign, digits, an
   optional fraction and an optional exponent. A stream is left just past
   the number. An integer mantissa with a non-negative exponent answers an
   Integer (1e3 = 1000), as the literal does; the rest is parsed by asNumber."
  | in token pos hasPoint exponent |
  in := aStringOrStream isString
    ifTrue: [ReadStream on: aStringOrStream]
    ifFalse: [aStringOrStream].
  in skipSeparators.
  token := WriteStream on: String new.
  in peek = $- ifTrue: [token nextPut: in next].
  (self readDigitsFrom: in on: token) = 0
    ifTrue: [^ self error: 'Reading a number failed: a digit was expected'].
  hasPoint := false.
  in peek = $. ifTrue: [
    pos := in position.
    in next.
    (in atEnd not and: [in peek isDigit])
      ifTrue: [token nextPut: $.. self readDigitsFrom: in on: token. hasPoint := true]
      ifFalse: [in position: pos]].
  exponent := nil.
  in peek = $e ifTrue: [
    pos := in position.
    in next.
    exponent := WriteStream on: String new.
    in peek = $- ifTrue: [exponent nextPut: in next].
    (self readDigitsFrom: in on: exponent) = 0
      ifTrue: [in position: pos. exponent := nil]].
  exponent isNil ifTrue: [^ token contents asNumber].
  exponent := exponent contents asNumber.
  (hasPoint not and: [exponent >= 0])
    ifTrue: [^ token contents asNumber * (10 raisedTo: exponent)].
  ^ (token contents , 'e' , exponent printString) asNumber.

Number class >> readDigitsFrom: in on: aStream
  "Private: copy the decimal digits at the stream's position onto aStream and
   answer how many there were."
  | count |
  count := 0.
  [in atEnd not and: [in peek isDigit]] whileTrue: [aStream nextPut: in next. count := count + 1].
  ^ count.
