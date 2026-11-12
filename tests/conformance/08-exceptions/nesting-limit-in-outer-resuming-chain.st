"EXPECT: 300 300|limit: stack depth exceeded (nested block evaluation) true|limit: stack depth exceeded (nested block evaluation) true|fast:true"
"§8.4 / S23: a chain of handlers that each answer (e outer + 1) by resuming.
 outer runs the enclosing handler on top of the one that sent it, so the
 chain nests a handler engine per level on top of the recursion. At 300
 levels it fits: every handler runs once and adds 1 to the outermost's 0.
 At 600 levels the recursion fits but the outer chain reaches the engine
 nesting limit on the way out: the handler that could not start receives the
 limit's catchable Error once the stack has unwound to its on:do: (S23), and
 answers its text; no handler runs twice. At 1,000 levels the recursion
 itself reaches the limit. Each run finishes promptly."
Object subclass: #OuterChain instanceVariableNames: 'runs'.
OuterChain >> init
  runs := 0.
OuterChain >> chain: n
  n = 0 ifTrue: [^ (Warning signal: 'bottom') + 0].
  ^ [self chain: n - 1]
      on: Warning, Error
      do: [:e |
        runs := runs + 1.
        (e isKindOf: Error)
          ifTrue: [e return: 'limit: ' , e messageText]
          ifFalse: [e resume: e outer + 1]].
OuterChain >> runs
  ^ runs.
| at a b c ms |
at := [:n | | chain r |
  chain := OuterChain new init.
  r := [chain chain: n] on: Warning do: [:e | e resume: 0].
  {r. chain runs <= n}].
ms := Time millisecondsToRun: [
  a := OuterChain new init.
  a := (([a chain: 300] on: Warning do: [:e | e resume: 0]) printString) , ' ' , a runs printString.
  b := at value: 600.
  c := at value: 1000].
a , '|' , b first displayString , ' ' , b last printString , '|' , c first displayString , ' ' , c last printString
  , '|fast:' , (ms < 5000) printString
