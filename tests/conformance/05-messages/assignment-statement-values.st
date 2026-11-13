"EXPECT: #(3 3 3 true 5 7 nil 9 #(1 2) 4)"
"An assignment statement whose value is unused is compiled without DUP and
 POP (0.10.0). Its value is still the assigned value wherever it is used:
 a nested assignment, the last statement of a block, of an inlined arm,
 a method without ^ answers self, a to:do: or whileTrue: body ending in an
 assignment, a captured variable and an instance variable."
Object subclass: #P instanceVariableNames: 'iv'.
P >> setIv iv := 5.
P >> iv ^ iv.
P >> chain | a b | a := b := 3. ^ a + b - 3.
P >> loop | s | s := 0. 1 to: 3 do: [:i | s := s + i. s := s + 0]. ^ s + 1.
| p r c blk |
p := P new.
c := 0.
blk := [ c := c + 9 ].
{ p chain.
  [ r := 3 ] value.
  (true ifTrue: [ r := 3 ] ifFalse: [ 0 ]).
  p setIv == p.
  p iv.
  p loop.
  [ r := r - 1. r > 0 ] whileTrue: [ c := c + 0 ].
  blk value.
  [ :x | | y | y := x. { y. y := x + 1 } ] value: 1.
  [ | q | q := 1. [ q := q + 3 ] value. q ] value }
