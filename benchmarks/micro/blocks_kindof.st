"blocks_kindof.st: the cost of a block that is not inlined and of isKindOf:
 (protoBaseST review P3), 10^5 iterations each inside a method. Every loop
 accumulates a result the program checks, so a failed or skipped loop cannot
 look fast. Prints: name, iterations, ms, ns per iteration; then
 'blocks_kindof ok'."
Object subclass: #BK.
BK subclass: #BK2.
BK2 subclass: #BK3.

BK >> baseline: n
  | s |
  s := 0.
  1 to: n do: [ :i | s := s + 1 ].
  ^ s.

BK >> sendOnly: n
  | s |
  s := 0.
  1 to: n do: [ :i | s := s + (self one) ].
  ^ s.

BK >> one
  ^ 1.

BK >> blockCreate: n
  | s b |
  s := 0.
  1 to: n do: [ :i | b := [ 1 ]. s := s + 1 ].
  ^ s.

BK >> blockValue: n
  | s |
  s := 0.
  1 to: n do: [ :i | s := s + [ 1 ] value ].
  ^ s.

BK >> blockCapture: n
  | s |
  s := 0.
  1 to: n do: [ :i | s := s + [ i ] value ].
  ^ s - (n * (n + 1) // 2) + n.

BK >> atIfAbsent: n
  | s d |
  s := 0. d := Dictionary new. d at: 1 put: 1.
  1 to: n do: [ :i | s := s + (d at: 1 ifAbsent: [ 0 ]) ].
  ^ s.

BK >> at: n
  | s d |
  s := 0. d := Dictionary new. d at: 1 put: 1.
  1 to: n do: [ :i | s := s + (d at: 1) ].
  ^ s.

BK >> kindOfHit: n
  | s o |
  s := 0. o := BK3 new.
  1 to: n do: [ :i | (o isKindOf: BK) ifTrue: [ s := s + 1 ] ].
  ^ s.

BK >> kindOfMiss: n
  | s o |
  s := 0. o := BK3 new.
  1 to: n do: [ :i | (o isKindOf: String) ifFalse: [ s := s + 1 ] ].
  ^ s.

BK >> kindOfSmallInt: n
  | s |
  s := 0.
  1 to: n do: [ :i | (i isKindOf: Integer) ifTrue: [ s := s + 1 ] ].
  ^ s.

BK >> classEq: n
  | s o |
  s := 0. o := BK3 new.
  1 to: n do: [ :i | o class == BK3 ifTrue: [ s := s + 1 ] ].
  ^ s.

BK >> handler: n
  | s |
  s := 0.
  1 to: n do: [ :i | s := s + ([ 1 ] on: Error do: [ :e | 0 ]) ].
  ^ s.

| b n |
b := BK new.
n := 100000.
#( #baseline: #sendOnly: #blockCreate: #blockValue: #blockCapture: #at: #atIfAbsent:
   #kindOfHit: #kindOfMiss: #kindOfSmallInt: #classEq: #handler: ) do: [ :sel | | r ms |
  ms := Time millisecondsToRun: [ r := b perform: sel with: n ].
  r = n ifFalse: [ Error signal: sel , ' answered ' , r printString ].
  (sel , ' ' , n printString , ' ' , ms printString , ' ms ' , (ms * 1000000 // n) printString , ' ns') displayNl ].
'blocks_kindof ok' displayNl.
