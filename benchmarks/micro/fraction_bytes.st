"fraction_bytes.st: the cost of Fraction comparisons and of reading an
 unsigned Integer out of a ByteArray, the two costs a protoBaseST profile
 measured (index order keys compare Fractions; oids are 16-byte big-endian
 unsigned Integers). Each operation runs in a method loop of N iterations
 over operands taken from prepared Arrays, and counts its results; the
 count is compared with a value computed independently before the timed
 loop (from the operand indices, or from a per-byte reference), so a loop
 that failed or computed something else cannot look fast. Prints one line
 per operation: name, iterations, nanoseconds per iteration (loop
 included; the two control lines measure the loop alone); then
 'fraction_bytes ok'. ByteArray>>unsignedIntegerFrom:to:bigEndian: is timed
 only where the image has it, so the same file runs before and after it was
 added."
Object subclass: #FracBench instanceVariableNames: 'fr big ints floats keys oids'.

FracBench >> setUp
  "fr: 64 strictly increasing Fractions k/(k+1); big: increasing Fractions
   whose numerators and denominators are beyond SmallInteger range; ints:
   the Integers -31 to 32; floats: each element of fr as a Float plus 1e-6,
   above it and below the next element; keys: 64 distinct 16-byte oids;
   oids: their values, by the per-byte reference."
  fr := (1 to: 64) collect: [:k | k / (k + 1)].
  big := (1 to: 64) collect: [:k | ((2 raisedTo: 80) * k + 1) / ((2 raisedTo: 80) * (k + 1) + 3)].
  ints := (1 to: 64) collect: [:k | k - 32].
  floats := fr collect: [:f | f asFloat + 0.000001].
  keys := (1 to: 64) collect: [:k |
    ByteArray withAll: ((1 to: 16) collect: [:j | (k * 37 + (j * 101)) \\ 256])].
  oids := keys collect: [:key | self oidByBytes: key].

FracBench >> ints ^ ints.

FracBench >> frSame: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) == (fr at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> frLt: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) < (fr at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> frLe: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) <= (fr at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> frGt: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) > (fr at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> frEq: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) = (fr at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> bigLt: n | s | s := 0. 1 to: n do: [:i | ((big at: i \\ 64 + 1) < (big at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> frLtInt: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) < (ints at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> intLtFr: n | s | s := 0. 1 to: n do: [:i | ((ints at: i * 7 \\ 64 + 1) < (fr at: i \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> frLtFloat: n | s | s := 0. 1 to: n do: [:i | ((fr at: i \\ 64 + 1) < (floats at: i * 7 \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> floatLtFr: n | s | s := 0. 1 to: n do: [:i | ((floats at: i * 7 \\ 64 + 1) < (fr at: i \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.

FracBench >> oidByBytes: aByteArray
  "The per-byte reference: the big-endian unsigned Integer, one byte at a time."
  ^ aByteArray inject: 0 into: [:acc :b | (acc bitShift: 8) + b].
FracBench >> oidByWords: aByteArray
  "The former workaround: two 64-bit reads, a shift and an add."
  ^ ((aByteArray unsignedLong64At: 1 bigEndian: true) bitShift: 64)
      + (aByteArray unsignedLong64At: 9 bigEndian: true).
FracBench >> oidSame: n | s | s := 0. 1 to: n do: [:i | ((oids at: i \\ 64 + 1) = (oids at: i \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> oidBytes: n | s | s := 0. 1 to: n do: [:i | ((self oidByBytes: (keys at: i \\ 64 + 1)) = (oids at: i \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> oidWords: n | s | s := 0. 1 to: n do: [:i | ((self oidByWords: (keys at: i \\ 64 + 1)) = (oids at: i \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.
FracBench >> oidPrim: n | s | s := 0. 1 to: n do: [:i | (((keys at: i \\ 64 + 1) unsignedIntegerFrom: 1 to: 16 bigEndian: true) = (oids at: i \\ 64 + 1)) ifTrue: [s := s + 1]]. ^ s.

| o n nBig count check checkN |
o := FracBench new.
o setUp.
n := 200000.
"Expected counts from the operand indices: fr and big are strictly increasing,
 so (fr at: j) < (fr at: k) exactly when j < k; every Fraction in fr lies in
 (0, 1), so it is below the Integer m exactly when m >= 1; floats at: k lies
 above fr at: k and below fr at: k + 1."
count := [:pred | | c | c := 0. 1 to: n do: [:i | (pred value: i \\ 64 + 1 value: i * 7 \\ 64 + 1) ifTrue: [c := c + 1]]. c].
checkN := [ :name :iterations :expected :blk | | r us |
  us := Time microsecondClockValue.
  r := blk value.
  us := Time microsecondClockValue - us.
  r = expected ifFalse: [ Error signal: name , ' answered ' , r printString , ', expected ' , expected printString ].
  (name , ' ' , iterations printString , ' ' , (us * 1000 // iterations) printString , ' ns') displayNl ].
check := [ :name :expected :blk | checkN value: name value: n value: expected value: blk ].
check value: 'control: Fraction == Fraction' value: (count value: [:j :k | j = k]) value: [ o frSame: n ].
check value: 'Fraction < Fraction' value: (count value: [:j :k | j < k]) value: [ o frLt: n ].
check value: 'Fraction <= Fraction' value: (count value: [:j :k | j <= k]) value: [ o frLe: n ].
check value: 'Fraction > Fraction' value: (count value: [:j :k | j > k]) value: [ o frGt: n ].
check value: 'Fraction = Fraction' value: (count value: [:j :k | j = k]) value: [ o frEq: n ].
"The former big comparison took milliseconds: it runs nBig iterations."
nBig := n // 100.
checkN value: 'big Fraction < big Fraction' value: nBig
  value: ((1 to: nBig) inject: 0 into: [:c :i | (i \\ 64) < (i * 7 \\ 64) ifTrue: [c + 1] ifFalse: [c]])
  value: [ o bigLt: nBig ].
check value: 'Fraction < Integer' value: (count value: [:j :k | (o ints at: k) >= 1]) value: [ o frLtInt: n ].
check value: 'Integer < Fraction' value: (count value: [:j :k | (o ints at: k) <= 0]) value: [ o intLtFr: n ].
check value: 'Fraction < Float' value: (count value: [:j :k | j <= k]) value: [ o frLtFloat: n ].
check value: 'Float < Fraction' value: (count value: [:j :k | k < j]) value: [ o floatLtFr: n ].
check value: 'control: oid = oid' value: n value: [ o oidSame: n ].
check value: '16-byte oid, per byte' value: n value: [ o oidBytes: n ].
check value: '16-byte oid, two 64-bit reads' value: n value: [ o oidWords: n ].
((ByteArray new: 1) respondsTo: #unsignedIntegerFrom:to:bigEndian:)
  ifTrue: [ check value: '16-byte oid, unsignedIntegerFrom:to:bigEndian:' value: n value: [ o oidPrim: n ] ].
'fraction_bytes ok' displayNl.
