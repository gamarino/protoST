"EXPECT: 0 differ of 1416 sums (400 vectors made here and the 72 CPython vector cases, 3 shapes each)"
"Float fsumOfPowSquaredDifferences: a with: b answers the same 8 bytes as Float fsum: ((1 to: n) collect: [:i | ((a at: i) - (b at: i)) raisedTo: 2]), the sum protoBaseST's k-means kernel writes, on every platform: both square with the C library's pow. The vectors are made here from a fixed linear congruential sequence: unit-range coordinates, spread exponents, negative differences, -0.0, and differences with 27 significant bits, whose squares are exact ties (where glibc's pow and d * d differ about half the time). The 72 cases of _vectors_fsum_pow.st are held to the same rule here, on every platform: on Windows, where MSVC's pow rounds some half-way squares otherwise than glibc's and float-fsum-pow-vectors.st cannot pin CPython-on-glibc bytes, this is the check those cases get."
Object subclass: #FsumSeq instanceVariableNames: 'items'.

FsumSeq class >> on: anArray
  ^ self new setItems: anArray.

FsumSeq >> setItems: anArray
  items := anArray.

FsumSeq >> size
  ^ items size.

FsumSeq >> at: anInteger
  ^ items at: anInteger.

Object subclass: #Lcg instanceVariableNames: 'seed'.

Lcg class >> seed: anInteger
  ^ self new setSeed: anInteger.

Lcg >> setSeed: anInteger
  seed := anInteger.

Lcg >> next
  "An integer in [0, 2^31)."
  seed := (seed * 1103515245 + 12345) \\ 2147483648.
  ^ seed.

Lcg >> unit
  "A Float in [-1, 1)."
  ^ (self next / 1073741824) asFloat - 1.0.

Lcg >> scale
  "A power of two between 2^-20 and 2^20."
  ^ 2.0 raisedTo: (self next \\ 41) - 20.

Lcg >> pairFor: kind
  "Two coordinates a and b."
  | b d |
  kind = 0 ifTrue: [^ { self unit. self unit }].
  kind = 1 ifTrue: [^ { self unit * self scale. self unit * self scale }].
  kind = 2 ifTrue: [
    "d with 27 significant bits: d * d is an exact tie; b is a small integer
     multiple of a power of two, so that b + d is exact."
    d := ((67108864 + (self next \\ 67108864)) bitOr: 1) asFloat * (2.0 raisedTo: (self next \\ 21) - 36).
    b := ((self next \\ 9) - 4) asFloat * (2.0 raisedTo: (self next \\ 5) - 2).
    ^ (self next \\ 2) = 0 ifTrue: [{ b + d. b }] ifFalse: [{ b. b + d }]].
  kind = 3 ifTrue: [b := self unit. ^ { b. b }].
  ^ { -0.0. self unit }.

Object subclass: #FsumCheck instanceVariableNames: ''.

FsumCheck class >> hexOf: aFloat
  | s |
  s := WriteStream on: ByteArray new.
  s nextPutFloat64: aFloat bigEndian: true.
  ^ s contents hex.

FsumCheck class >> answer: aBlock
  ^ [self hexOf: aBlock value] on: Error do: [:e | e messageText].

FsumCheck class >> floats: hexes
  ^ hexes collect: [:h | (ByteArray fromHex: h) float64At: 1 bigEndian: true].

FsumCheck class >> check: xs with: ys named: aString into: names
  "The number of shapes (of 3) whose primitive answer differs from the
   raisedTo: 2 sum."
  | reference shapesA shapesB differ |
  reference := self answer: [Float fsum: ((1 to: xs size) collect: [:i | ((xs at: i) - (ys at: i)) raisedTo: 2])].
  shapesA := { xs. (OrderedCollection withAll: xs). (FsumSeq on: xs) }.
  shapesB := { ys. (OrderedCollection withAll: ys). (FsumSeq on: ys) }.
  differ := 0.
  1 to: 3 do: [:k | | got |
    got := self answer: [Float fsumOfPowSquaredDifferences: (shapesA at: k) with: (shapesB at: k)].
    got = reference ifFalse: [
      differ := differ + 1.
      names nextPutAll: ' [' , aString , ' #' , k printString , ' got ' , got , ' raisedTo: ' , reference , ']']].
  ^ differ.

| rng differ sums names |
rng := Lcg seed: 20261121.
differ := 0.
sums := 0.
names := WriteStream on: String new.
1 to: 400 do: [:v |
  | n xs ys |
  n := (rng next \\ 48) + 1.
  xs := Array new: n.
  ys := Array new: n.
  1 to: n do: [:i | | pair |
    pair := rng pairFor: (v \\ 7 < 3 ifTrue: [2] ifFalse: [rng next \\ 5]).
    xs at: i put: (pair at: 1).
    ys at: i put: (pair at: 2)].
  sums := sums + 3.
  differ := differ + (FsumCheck check: xs with: ys named: 'vector ' , v printString into: names)].
(Import from: '_vectors_fsum_pow') VectorsFsumPow sqpows do: [:e |
  sums := sums + 3.
  differ := differ + (FsumCheck check: (FsumCheck floats: (e at: 2)) with: (FsumCheck floats: (e at: 3))
    named: (e at: 1) into: names)].
differ printString , ' differ of ' , sums printString , ' sums (400 vectors made here and the 72 CPython vector cases, 3 shapes each)' , names contents.
