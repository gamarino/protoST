"EXPECT: 1 2 2 outer:from pair1 outer:passed 3 7"
"on:do:on:do: searches its pairs in written order, the first pair first, as
 the clauses of a try/catch: both pairs match ZeroDivide, so the first one
 written answers (1, then 2 with the pairs swapped); a first pair that does
 not match falls through to the second (2). As in a try/catch, the pairs of
 one construct never catch what a sibling's handler signals or passes: that
 goes to the enclosing handler. retry and resume: act on the construct as on
 on:do:. Until 0.9.0 the second pair was searched first, and a handler's
 signal was caught by its sibling."
| out n |
out := WriteStream on: String new.
out print: ([1/0] on: ZeroDivide do: [:e | 1] on: Error do: [:e | 2]); space.
out print: ([1/0] on: Error do: [:e | 2] on: ZeroDivide do: [:e | 1]); space.
out print: ([1/0] on: MessageNotUnderstood do: [:e | 1] on: ZeroDivide do: [:e | 2]); space.
out nextPutAll: ([[1/0] on: ZeroDivide do: [:e | Error signal: 'from pair1'] on: Error do: [:e | 'sibling']]
  on: Error do: [:e | 'outer:' , e messageText]); space.
out nextPutAll: ([[1/0] on: ZeroDivide do: [:e | e pass] on: ArithmeticError do: [:e | 'sibling']]
  on: ZeroDivide do: [:e | 'outer:passed']); space.
n := 0.
out print: ([n := n + 1. n < 3 ifTrue: [1/0]. n] on: Warning do: [:e | 0] on: ZeroDivide do: [:e | e retry]); space.
out print: ([Warning signal: 'w'] on: ZeroDivide do: [:e | 1] on: Warning do: [:e | e resume: 7]).
out contents
