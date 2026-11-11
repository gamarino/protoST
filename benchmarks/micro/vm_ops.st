"vm_ops.st: the per-operation costs of the 2026-11-11 performance-gap
 analysis (Array new:, at:, at:put:, SmallInteger arithmetic, instance
 variable store, sends, block value:, perform:with:), each in a method loop
 of N iterations. Every loop folds its results into a checksum that the
 program compares with the expected value, so a loop that failed, was
 skipped or computed something else cannot look fast. Prints one line per
 operation: name, iterations, nanoseconds per iteration (loop included);
 then 'vm_ops ok'. Under perf stat the totals are what to compare."
Object subclass: #VmOps instanceVariableNames: 'iv'.

VmOps >> arrayNew: n | s | s := 0. 1 to: n do: [:i | s := s + (Array new: 4) size]. ^ s.
VmOps >> arrayAt: n | s a | s := 0. a := Array new: 4 withAll: 7. 1 to: n do: [:i | s := s + (a at: 2)]. ^ s.
VmOps >> arrayAtPut: n | a | a := Array new: 4. 1 to: n do: [:i | a at: 3 put: i]. ^ a at: 3.
VmOps >> mul: n | s | s := 0. 1 to: n do: [:i | s := s + (i * 2)]. ^ s.
VmOps >> mod: n | s | s := 0. 1 to: n do: [:i | s := s + (i \\ 7)]. ^ s.
VmOps >> quo: n | s | s := 0. 1 to: n do: [:i | s := s + (i // 7)]. ^ s.
VmOps >> rem: n | s | s := 0. 1 to: n do: [:i | s := s + (i rem: 7)]. ^ s.
VmOps >> band: n | s | s := 0. 1 to: n do: [:i | s := s + (i bitAnd: 7) + (i bitOr: 1)]. ^ s.
VmOps >> ivarWrite: n 1 to: n do: [:i | iv := i]. ^ iv.
VmOps >> id: k ^ k.
VmOps >> send: n | s | s := 0. 1 to: n do: [:i | s := s + (self id: i)]. ^ s.
VmOps >> blockValue: n | s b | s := 0. b := [:x | x]. 1 to: n do: [:i | s := s + (b value: i)]. ^ s.
VmOps >> perform: n | s | s := 0. 1 to: n do: [:i | s := s + (self perform: #id: with: i)]. ^ s.

| o n tri check |
o := VmOps new.
n := 300000.
tri := n * (n + 1) // 2.
check := [ :name :expected :blk | | r us |
  us := Time microsecondClockValue.
  r := blk value.
  us := Time microsecondClockValue - us.
  r = expected ifFalse: [ Error signal: name , ' answered ' , r printString , ', expected ' , expected printString ].
  (name , ' ' , n printString , ' ' , (us * 1000 // n) printString , ' ns') displayNl ].
check value: 'Array new:' value: 4 * n value: [ o arrayNew: n ].
check value: 'Array at:' value: 7 * n value: [ o arrayAt: n ].
check value: 'Array at:put:' value: n value: [ o arrayAtPut: n ].
check value: '*' value: 2 * tri value: [ o mul: n ].
check value: '\\' value: ((1 to: n) inject: 0 into: [ :a :i | a + (i \\ 7) ]) value: [ o mod: n ].
check value: '//' value: ((1 to: n) inject: 0 into: [ :a :i | a + (i // 7) ]) value: [ o quo: n ].
check value: 'rem:' value: ((1 to: n) inject: 0 into: [ :a :i | a + (i rem: 7) ]) value: [ o rem: n ].
check value: 'bitAnd: bitOr:' value: ((1 to: n) inject: 0 into: [ :a :i | a + (i bitAnd: 7) + (i bitOr: 1) ]) value: [ o band: n ].
check value: 'ivar write' value: n value: [ o ivarWrite: n ].
check value: 'send' value: tri value: [ o send: n ].
check value: 'block value:' value: tri value: [ o blockValue: n ].
check value: 'perform:with:' value: tri value: [ o perform: n ].
'vm_ops ok' displayNl.
