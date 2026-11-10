"EXPECT: 7 8 true orig"
"§10 within: / waitWithin: settle like the original Future when it settles
 first. An already resolved Future answers its value even with a zero
 deadline. A Future resolved after about 20 ms answers its value under
 waitWithin: 2000, well before the deadline (generous bound: under 1500 ms).
 An already rejected Future re-signals its own error, not TimeoutError."
| resolved later a b ms rejected c |
resolved := Future new.
resolved resolve: 7.
a := resolved waitWithin: 0.
later := Future new.
(Future after: 20) thenDo: [:x | later resolve: 8].
ms := Time millisecondsToRun: [b := later waitWithin: 2000].
rejected := Future new.
rejected rejectWith: (Error new messageText: 'orig').
c := [[rejected waitWithin: 0] on: TimeoutError do: [:e | 'wrong: timeout']]
  on: Error do: [:e | e messageText].
a printString , ' ' , b printString , ' ' , (ms < 1500) printString , ' ' , c
