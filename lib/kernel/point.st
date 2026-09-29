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

Point >> hash
  ^ (x hash * 31 + y hash) \\ 1073741824.

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
