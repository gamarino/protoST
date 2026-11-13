"Kernel: Point, written in protoST. x @ y answers a Point."

Object subclass: #Point instanceVariableNames: 'x y'.

Point class >> x: ax y: ay
  ^ self basicNew setX: ax y: ay.

Point >> setX: ax y: ay
  x := ax.
  y := ay.
  ^ self.

Point >> x
  ^ x.

Point >> y
  ^ y.

Point >> + aPointOrNumber
  | p |
  p := aPointOrNumber asPoint.
  ^ Point x: x + p x y: y + p y.

Point >> - aPointOrNumber
  | p |
  p := aPointOrNumber asPoint.
  ^ Point x: x - p x y: y - p y.

Point >> * aPointOrNumber
  | p |
  p := aPointOrNumber asPoint.
  ^ Point x: x * p x y: y * p y.

Point >> / aPointOrNumber
  | p |
  p := aPointOrNumber asPoint.
  ^ Point x: x / p x y: y / p y.

Point >> = other
  ^ (other isKindOf: Point) and: [x = other x and: [y = other y]].

"The hash is (x hash * 1000003 + y hash) \\ P, P = 2^61 - 1, the numeric
 hash's own modulus (R3): a Float's hash keeps its fraction bits high, and a
 smaller modulus would drop them (i/4.0 @ j/4.0 Points used to share 1/16 of
 the buckets). The multiplier is large (1000003, as CPython's tuple hash had
 before 3.8): the hashes of k/4.0 are k * 2^59 modulo P, so with 31 the
 Points (i, j) and (i + 1, j - 31) had equal hashes. __hashMix: computes it
 natively, without boxing the coordinates' hashes as LargeIntegers."
Point >> hash
  ^ x __hashMix: y.

Point >> dist: aPoint
  | dx dy |
  dx := aPoint x - x.
  dy := aPoint y - y.
  ^ ((dx * dx) + (dy * dy)) sqrt.

Point >> asPoint
  ^ self.

Point >> printOn: aStream
  aStream print: x; nextPut: $@; print: y.

Number >> @ y
  ^ Point x: self y: y.

Number >> asPoint
  ^ Point x: self y: self.
