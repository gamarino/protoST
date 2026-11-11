"-- decimal.st: the exact Decimal value --

An exact decimal number: a sign, a coefficient and a power-of-ten exponent,
or one of the special values Infinity, NaN and sNaN. The model and the text
are those of the General Decimal Arithmetic specification, as Python's
decimal module implements them; the text is the ProtoBase format spec's
'Decimal text' (the @dec value of format 4).

A Decimal is a value without arithmetic (STATUS D49): it parses, prints,
compares and converts to an exact fraction. There is no context, no
precision and no rounding, so nothing is ever lost: 1.20 keeps its trailing
zero and -0 its sign.

USAGE
  m := Import from: 'decimal'.            (binds the global Decimal too)
  d := Decimal fromString: '1.20'.
  d printString.                          => '1.20'
  d sign. d coefficient. d exponent.      => 0 120 -2
  d asExactFraction.                      => (6/5)
  d = 1.2.                                => false (1.2 is a binary Float)
  d = (6/5).                              => true
  d sameRepresentationAs: (Decimal fromString: '1.2').   => false

TEXT
  fromString: accepts Python's Decimal(str) grammar, ASCII and
  case-insensitive: an optional sign, then digits with an optional point and
  an optional exponent (e or E, an optional sign, digits), or Infinity / Inf,
  or NaN / sNaN followed by optional payload digits. Surrounding whitespace,
  underscores and non-ASCII digits, which Python also accepts, are refused.
  Anything else signals Error, whose messageText names the text.

  printString is the to-scientific-string: with c the coefficient's digits
  (n of them), e the exponent and a = e + n - 1 the adjusted exponent, the
  plain form when e <= 0 and a >= -6, else d.dddE+a. Specials print as
  Infinity, NaN<payload> and sNaN<payload> (no payload digits when it is 0),
  with a leading - for sign 1.

COMPARISON
  =, ~=, <, <=, >, >= take any Number (Integer, Fraction, Float) or another
  Decimal and compare exact values, in both directions (a number receiver
  dispatches back through adaptToNumber:andSend:, so 1 = aDecimal works): a finite Float is read through its
  asExactFraction, so (Decimal fromString: '0.1') = 0.1 is false. The
  infinities order at the ends and equal the Float infinities. A NaN (quiet
  or signaling, Decimal or Float) is never =, and any ordering involving one
  signals Error; so does ordering against a non-Number. hash agrees with =
  across Integer, Fraction and Float. sameRepresentationAs: compares the
  representation instead: kind, sign, coefficient (or payload) and exponent.

The value is immutable: the only initialiser is private (_setSign:...),
used by the class-side constructors, and refuses a second call."

Object subclass: #Decimal
  instanceVariableNames: 'sign coefficient exponent kind'.

"=== class side: constructors ==============================================="

"A finite decimal: (-1)^sign * coefficient * 10^exponent."
Decimal class >> sign: aSign coefficient: aCoefficient exponent: anExponent
  self _checkSign: aSign.
  (aCoefficient isInteger and: [aCoefficient >= 0]) ifFalse: [
    ^ Error signal: 'Decimal: the coefficient must be a non-negative Integer, not '
      , aCoefficient printString ].
  anExponent isInteger ifFalse: [
    ^ Error signal: 'Decimal: the exponent must be an Integer, not ' , anExponent printString ].
  ^ self basicNew _setSign: aSign coefficient: aCoefficient exponent: anExponent kind: #finite.

"Infinity (sign 0) or -Infinity (sign 1)."
Decimal class >> infinity: aSign
  self _checkSign: aSign.
  ^ self basicNew _setSign: aSign coefficient: 0 exponent: 0 kind: #infinite.

"A NaN with a non-negative Integer payload; signaling when aBoolean is true."
Decimal class >> nan: aPayload signaling: aBoolean sign: aSign
  self _checkSign: aSign.
  (aPayload isInteger and: [aPayload >= 0]) ifFalse: [
    ^ Error signal: 'Decimal: a NaN payload must be a non-negative Integer, not '
      , aPayload printString ].
  (aBoolean == true or: [aBoolean == false]) ifFalse: [
    ^ Error signal: 'Decimal: signaling: must be a Boolean, not ' , aBoolean printString ].
  ^ self basicNew
      _setSign: aSign coefficient: aPayload exponent: 0
      kind: (aBoolean ifTrue: [#snan] ifFalse: [#nan]).

Decimal class >> _checkSign: aSign
  (aSign == 0 or: [aSign == 1]) ifFalse: [
    ^ Error signal: 'Decimal: the sign must be 0 or 1, not ' , aSign printString ].
  ^ aSign.

"=== class side: parsing ===================================================="

"Parse Python's Decimal(str) syntax (see the module comment). Signals Error,
 naming the text, when aString is not a decimal string."
Decimal class >> fromString: aString
  | codes size i sign rest digitCount fracDigits coeff expSign expDigits seenPoint c |
  aString isString ifFalse: [
    ^ Error signal: 'Decimal fromString: a String was expected, not ' , aString printString ].
  codes := OrderedCollection new.
  1 to: aString size do: [:k | codes add: (self _fold: (aString at: k) asInteger)].
  size := codes size.
  i := 1.
  sign := 0.
  (i <= size and: [(codes at: i) = 43 or: [(codes at: i) = 45]]) ifTrue: [
    (codes at: i) = 45 ifTrue: [sign := 1].
    i := i + 1].
  rest := String streamContents: [:w |
    i to: size do: [:k | w nextPut: (codes at: k) asCharacter]].
  "Special values."
  (rest = 'inf' or: [rest = 'infinity']) ifTrue: [^ self infinity: sign].
  ((rest size >= 3 and: [(rest copyFrom: 1 to: 3) = 'nan'])
      or: [rest size >= 4 and: [(rest copyFrom: 1 to: 4) = 'snan']]) ifTrue: [
    | signaling start payload |
    signaling := (rest at: 1) = $s.
    start := signaling ifTrue: [5] ifFalse: [4].
    payload := 0.
    start to: rest size do: [:k |
      c := (rest at: k) asInteger.
      (self _isDigit: c) ifFalse: [^ self _invalid: aString].
      payload := payload * 10 + (c - 48)].
    ^ self nan: payload signaling: signaling sign: sign].
  "A finite number: digits [. digits] [e [sign] digits]."
  digitCount := 0. fracDigits := 0. coeff := 0.
  seenPoint := false.
  expSign := 1. expDigits := nil.
  [i <= size and: [(self _isDigit: (codes at: i)) or: [(codes at: i) = 46 and: [seenPoint not]]]]
    whileTrue: [
      c := codes at: i.
      c = 46
        ifTrue: [seenPoint := true]
        ifFalse: [
          coeff := coeff * 10 + (c - 48).
          digitCount := digitCount + 1.
          seenPoint ifTrue: [fracDigits := fracDigits + 1]].
      i := i + 1].
  digitCount = 0 ifTrue: [^ self _invalid: aString].
  (i <= size and: [(codes at: i) = 101]) ifTrue: [
    i := i + 1.
    (i <= size and: [(codes at: i) = 43 or: [(codes at: i) = 45]]) ifTrue: [
      (codes at: i) = 45 ifTrue: [expSign := -1].
      i := i + 1].
    expDigits := 0.
    (i <= size and: [self _isDigit: (codes at: i)]) ifFalse: [^ self _invalid: aString].
    [i <= size and: [self _isDigit: (codes at: i)]] whileTrue: [
      expDigits := expDigits * 10 + ((codes at: i) - 48).
      i := i + 1]].
  i <= size ifTrue: [^ self _invalid: aString].
  ^ self
      sign: sign
      coefficient: coeff
      exponent: (expDigits isNil ifTrue: [0] ifFalse: [expSign * expDigits]) - fracDigits.

"ASCII case folding of one code point: A-Z to a-z, everything else as is."
Decimal class >> _fold: aCode
  ^ (aCode >= 65 and: [aCode <= 90]) ifTrue: [aCode + 32] ifFalse: [aCode].

Decimal class >> _isDigit: aCode
  ^ aCode >= 48 and: [aCode <= 57].

Decimal class >> _invalid: aString
  ^ Error signal: 'Decimal fromString: not a decimal string: ' , aString printString.

"=== private initialiser ===================================================="

Decimal >> _setSign: aSign coefficient: aCoefficient exponent: anExponent kind: aKind
  "Runs once, on a fresh instance: a live Decimal never changes."
  kind isNil ifFalse: [^ Error signal: 'Decimal is immutable: ' , self printString].
  sign := aSign.
  coefficient := aCoefficient.
  exponent := anExponent.
  kind := aKind.
  ^ self.

"=== accessors =============================================================="

"0 for a positive value, 1 for a negative one (-0 included)."
Decimal >> sign
  ^ sign.

"The coefficient of a finite value; nil for Infinity and NaN."
Decimal >> coefficient
  ^ kind == #finite ifTrue: [coefficient] ifFalse: [nil].

"The exponent of a finite value; nil for Infinity and NaN."
Decimal >> exponent
  ^ kind == #finite ifTrue: [exponent] ifFalse: [nil].

"The payload of a NaN (0 when it has none); nil for any other value."
Decimal >> payload
  ^ self isNaN ifTrue: [coefficient] ifFalse: [nil].

Decimal >> isFinite
  ^ kind == #finite.

Decimal >> isInfinite
  ^ kind == #infinite.

"True for a quiet and for a signaling NaN."
Decimal >> isNaN
  ^ kind == #nan or: [kind == #snan].

Decimal >> isSignaling
  ^ kind == #snan.

Decimal >> isZero
  ^ kind == #finite and: [coefficient = 0].

"=== printing ==============================================================="

"The to-scientific-string (Python's str(Decimal))."
Decimal >> printOn: aStream
  | digits n adjusted point |
  self isClassObject ifTrue: [^ aStream nextPutAll: self name].
  sign = 1 ifTrue: [aStream nextPut: $-].
  kind == #infinite ifTrue: [^ aStream nextPutAll: 'Infinity'].
  self isNaN ifTrue: [
    aStream nextPutAll: (kind == #snan ifTrue: ['sNaN'] ifFalse: ['NaN']).
    coefficient = 0 ifFalse: [aStream nextPutAll: coefficient printString].
    ^ self].
  digits := coefficient printString.
  n := digits size.
  adjusted := exponent + n - 1.
  (exponent <= 0 and: [adjusted >= -6]) ifTrue: [
    exponent = 0 ifTrue: [^ aStream nextPutAll: digits].
    point := n + exponent.
    point > 0
      ifTrue: [
        aStream nextPutAll: (digits copyFrom: 1 to: point);
          nextPut: $.;
          nextPutAll: (digits copyFrom: point + 1 to: n)]
      ifFalse: [
        aStream nextPutAll: '0.'.
        point negated timesRepeat: [aStream nextPut: $0].
        aStream nextPutAll: digits].
    ^ self].
  aStream nextPut: (digits at: 1).
  n > 1 ifTrue: [aStream nextPut: $.; nextPutAll: (digits copyFrom: 2 to: n)].
  aStream nextPut: $E; nextPut: (adjusted >= 0 ifTrue: [$+] ifFalse: [$-]);
    nextPutAll: adjusted abs printString.

"=== exact value ============================================================"

"The exact value of a finite decimal: an Integer or a reduced Fraction.
 Infinity and NaN signal Error."
Decimal >> asExactFraction
  | magnitude |
  kind == #finite ifFalse: [
    ^ Error signal: 'Decimal asExactFraction: ' , self printString , ' has no exact value' ].
  magnitude := exponent >= 0
    ifTrue: [coefficient * (10 raisedTo: exponent)]
    ifFalse: [coefficient / (10 raisedTo: exponent negated)].
  ^ sign = 1 ifTrue: [magnitude negated] ifFalse: [magnitude].

"=== comparison ============================================================="

Decimal >> = anObject
  | order |
  self isClassObject ifTrue: [^ self == anObject].
  order := self _compareWith: anObject.
  ^ order == 0.

Decimal >> ~= anObject
  ^ (self = anObject) not.

"Double dispatch from the numeric primitives and from Fraction, when a
 number is the receiver and a Decimal the argument (1 = aDecimal,
 (1/2) < aDecimal): the comparison is answered from the Decimal's side, so
 it is symmetric. Arithmetic is refused (D49)."
Decimal >> adaptToNumber: aNumber andSend: aSelector
  aSelector == #= ifTrue: [^ self = aNumber].
  aSelector == #~= ifTrue: [^ self ~= aNumber].
  aSelector == #< ifTrue: [^ self > aNumber].
  aSelector == #> ifTrue: [^ self < aNumber].
  aSelector == #<= ifTrue: [^ self >= aNumber].
  aSelector == #>= ifTrue: [^ self <= aNumber].
  ^ Error signal: 'Decimal has no arithmetic (STATUS D49): ' , aNumber printString , ' '
    , aSelector , ' ' , self printString , '; use asExactFraction'.

Decimal >> < anObject
  ^ (self _orderWith: anObject) < 0.

Decimal >> <= anObject
  ^ (self _orderWith: anObject) <= 0.

Decimal >> > anObject
  ^ (self _orderWith: anObject) > 0.

Decimal >> >= anObject
  ^ (self _orderWith: anObject) >= 0.

"Agrees with = : an equal Integer, Fraction or Float has the same hash."
Decimal >> hash
  self isClassObject ifTrue: [^ self identityHash].
  kind == #infinite ifTrue: [
    ^ sign = 0 ifTrue: [Float infinity hash] ifFalse: [Float infinity negated hash]].
  self isNaN ifTrue: [^ coefficient hash].
  coefficient = 0 ifTrue: [^ 0 hash].
  ^ self asExactFraction hash.

"Same kind, sign, coefficient (or payload) and exponent: 1.0 and 1.00 are
 equal (=) but not the same representation."
Decimal >> sameRepresentationAs: anObject
  (anObject isKindOf: Decimal) ifFalse: [^ false].
  ^ kind == anObject _kind
    and: [sign = anObject sign
    and: [coefficient = anObject _rawCoefficient
    and: [exponent = anObject _rawExponent]]].

Decimal >> _kind
  ^ kind.

Decimal >> _rawCoefficient
  ^ coefficient.

Decimal >> _rawExponent
  ^ exponent.

"-1, 0 or 1 for an ordering; signals Error when there is none."
Decimal >> _orderWith: anObject
  | order |
  order := self _compareWith: anObject.
  order == #unordered ifTrue: [
    ^ Error signal: 'Decimal: cannot order ' , self printString , ' and '
      , anObject printString , ' (a NaN has no order)' ].
  order == #incomparable ifTrue: [
    ^ Error signal: 'Decimal: cannot compare ' , self printString , ' with '
      , anObject printString ].
  ^ order.

"-1, 0 or 1 when the receiver is less than, equal to or greater than
 anObject; #unordered when either is a NaN; #incomparable when anObject is
 neither a Decimal nor a Number."
Decimal >> _compareWith: anObject
  | otherIsDecimal otherRank myRank |
  otherIsDecimal := (anObject isKindOf: Decimal) and: [anObject isClassObject not].
  (otherIsDecimal or: [anObject isNumber]) ifFalse: [^ #incomparable].
  (otherIsDecimal not and: [(anObject isFloat or: [anObject isInteger or: [anObject isFraction]]) not])
    ifTrue: [^ #incomparable].
  (self isNaN or: [anObject isNaN]) ifTrue: [^ #unordered].
  "Infinities order at the ends: rank -1, 0 (finite) or 1."
  myRank := self _rank.
  otherRank := otherIsDecimal
    ifTrue: [anObject _rank]
    ifFalse: [anObject isFloat
      ifTrue: [anObject isFinite ifTrue: [0] ifFalse: [anObject > 0 ifTrue: [1] ifFalse: [-1]]]
      ifFalse: [0]].
  (myRank ~= 0 or: [otherRank ~= 0]) ifTrue: [^ (myRank - otherRank) sign].
  otherIsDecimal ifTrue: [^ self _compareWithDecimal: anObject].
  ^ self _compareWithRational: anObject asExactFraction.

Decimal >> _rank
  kind == #infinite ifFalse: [^ 0].
  ^ sign = 0 ifTrue: [1] ifFalse: [-1].

"The sign of the value of a finite decimal: -1, 0 or 1 (-0 is 0)."
Decimal >> _valueSign
  coefficient = 0 ifTrue: [^ 0].
  ^ sign = 0 ifTrue: [1] ifFalse: [-1].

"The adjusted exponent of a finite non-zero decimal: the power of ten of its
 leading digit."
Decimal >> _adjusted
  ^ exponent + coefficient printString size - 1.

"Both finite. Decides on signs, then adjusted exponents, then the
 coefficients aligned to one exponent; never builds a power of ten larger
 than the coefficients' own digit counts."
Decimal >> _compareWithDecimal: aDecimal
  | mine theirs a1 a2 e c1 c2 |
  mine := self _valueSign.
  theirs := aDecimal _valueSign.
  mine ~= theirs ifTrue: [^ (mine - theirs) sign].
  mine = 0 ifTrue: [^ 0].
  a1 := self _adjusted.
  a2 := aDecimal _adjusted.
  a1 ~= a2 ifTrue: [^ (a1 - a2) sign * mine].
  e := exponent min: aDecimal _rawExponent.
  c1 := coefficient * (10 raisedTo: exponent - e).
  c2 := aDecimal _rawCoefficient * (10 raisedTo: aDecimal _rawExponent - e).
  ^ (c1 - c2) sign * mine.

"The receiver is finite; aRational is an Integer or a Fraction. Bounds on
 digit counts settle magnitudes far apart (|x| lies in
 (10^(np-nq-1), 10^(np-nq+1)) for np digits of |numerator| and nq of the
 denominator); only magnitudes that close are compared exactly, so a huge
 exponent never builds a huge power of ten."
Decimal >> _compareWithRational: aRational
  | mine theirs p q np nq a num den |
  mine := self _valueSign.
  theirs := aRational sign.
  mine ~= theirs ifTrue: [^ (mine - theirs) sign].
  mine = 0 ifTrue: [^ 0].
  p := aRational numerator abs.
  q := aRational denominator.
  np := p printString size.
  nq := q printString size.
  a := self _adjusted.
  a >= (np - nq + 1) ifTrue: [^ mine].
  a <= (np - nq - 2) ifTrue: [^ mine negated].
  exponent >= 0
    ifTrue: [num := coefficient * (10 raisedTo: exponent). den := 1]
    ifFalse: [num := coefficient. den := 10 raisedTo: exponent negated].
  ^ ((num * q) - (p * den)) sign * mine.
