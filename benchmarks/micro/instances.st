"instances.st: the cost of creating instances of user classes and of reading
 and writing their instance variables, the paths protoCore's shared object
 shapes (ProtoShape, ProtoObject::newClone) change. Each operation runs in a
 method loop of N iterations and folds its results into a checksum that the
 program compares with the expected value, so a loop that failed, was skipped
 or computed something else cannot look fast. Prints one line per operation:
 name, iterations, nanoseconds per iteration (loop included); then
 'instances ok'. Under perf stat the totals are what to compare."
Object subclass: #P4 instanceVariableNames: 'a b c d'.
P4 >> a ^ a.
P4 >> b ^ b.
P4 >> c ^ c.
P4 >> d ^ d.
P4 >> setA: x b: y c: z d: w a := x. b := y. c := z. d := w.
P4 >> sum ^ a + b + c + d.
P4 >> bump a := a + 1.

P4 subclass: #P8 instanceVariableNames: 'e f g h'.
P8 >> setE: x f: y g: z h: w e := x. f := y. g := z. h := w.
P8 >> sum8 ^ a + b + c + d + e + f + g + h.

Object subclass: #InstBench instanceVariableNames: 'pool'.

"A bare instance: the allocator and nothing else."
InstBench >> newBare: n | s | s := 0. 1 to: n do: [:i | P4 new. s := s + 1]. ^ s.
"A four-variable instance, initialised by one method."
InstBench >> newInit: n | s | s := 0. 1 to: n do: [:i | s := s + (P4 new setA: i b: 1 c: 2 d: 3) a]. ^ s.
"A subclass instance: four inherited and four own variables."
InstBench >> newSub: n | s p | s := 0. 1 to: n do: [:i |
    p := P8 new. p setA: 1 b: 1 c: 1 d: 1. p setE: 1 f: 1 g: 1 h: i. s := s + p sum8]. ^ s.
"Reads of four variables across 1000 distinct instances (shape sharing)."
InstBench >> fill
    pool := Array new: 1000.
    1 to: 1000 do: [:i | pool at: i put: (P4 new setA: i b: 1 c: 2 d: 3)].
InstBench >> readMany: n | s k | s := 0. k := 0.
    1 to: n do: [:i | k := k \\ 1000 + 1. s := s + (pool at: k) sum]. ^ s.
"A write to one variable across 1000 distinct instances."
InstBench >> writeMany: n | k | k := 0.
    1 to: n do: [:i | k := k \\ 1000 + 1. (pool at: k) bump]. ^ n.
"A closure capturing a temporary."
InstBench >> closures: n | s t | s := 0. t := 1.
    1 to: n do: [:i | s := s + ([:x | x + t] value: i)]. ^ s.

| o n tri check expectedRead |
o := InstBench new.
n := 300000.
tri := n * (n + 1) // 2.
check := [ :name :expected :blk | | r us |
  us := Time microsecondClockValue.
  r := blk value.
  us := Time microsecondClockValue - us.
  r = expected ifFalse: [ Error signal: name , ' answered ' , r printString , ', expected ' , expected printString ].
  (name , ' ' , n printString , ' ' , (us * 1000 // n) printString , ' ns') displayNl ].
check value: 'new (bare)' value: n value: [ o newBare: n ].
check value: 'new + 4 ivars' value: tri value: [ o newInit: n ].
check value: 'subclass new + 8 ivars' value: tri + (7 * n) value: [ o newSub: n ].
o fill.
"Instance k contributes k + 6; n reads walk the pool n // 1000 times."
expectedRead := (n // 1000) * ((1000 * 1001 // 2) + (6 * 1000)).
check value: 'ivar read x4, 1000 objects' value: expectedRead value: [ o readMany: n ].
check value: 'ivar write, 1000 objects' value: n value: [ o writeMany: n ].
check value: 'closure create + value:' value: tri + n value: [ o closures: n ].
'instances ok' displayNl.
