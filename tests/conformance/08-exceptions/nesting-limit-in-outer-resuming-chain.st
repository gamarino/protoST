"EXPECT: 300 once:true all:true|limit: stack depth exceeded (nested block evaluation) once:true innermost:true|limit: stack depth exceeded (nested block evaluation) one:true|fast:true"
"§8.4 / S23: a chain of handlers that each answer (e outer + 1) by resuming.
 outer runs the enclosing handler on top of the one that sent it, so the
 chain nests a handler engine per level on top of the recursion. Each
 handler records its level; once: no level appears twice (no handler ran
 twice).
   300 levels fit: every handler runs exactly once (all: levels 1..300) and
     adds 1 to the outermost's 0.
   600 levels: the recursion fits but the outer chain reaches the engine
     nesting limit on the way out. The handler that could not start receives
     the limit's catchable Error once the stack has unwound to its on:do:
     (S23) and answers its text; the handlers that ran are exactly the
     innermost levels 1..k, each once (innermost:), and k < 600.
   1,000 levels: the recursion itself reaches the limit, inside the handler
     of the deepest level it established; that handler alone runs, once
     (one:), and answers the limit's text.
 Each run finishes promptly."
Object subclass: #OuterChain instanceVariableNames: 'levels'.
OuterChain >> init
  levels := OrderedCollection new.
OuterChain >> chain: n
  n = 0 ifTrue: [^ (Warning signal: 'bottom') + 0].
  ^ [self chain: n - 1]
      on: Warning, Error
      do: [:e |
        levels add: n.
        (e isKindOf: Error)
          ifTrue: [e return: 'limit: ' , e messageText]
          ifFalse: [e resume: e outer + 1]].
OuterChain >> levels
  ^ levels.
| run a b c ms once innermost |
once := [:chain | chain levels asSet size = chain levels size].
innermost := [:chain :n | | k |
  k := chain levels size.
  k < n and: [chain levels asSet = (1 to: k) asSet]].
run := [:n | | chain r |
  chain := OuterChain new init.
  r := [chain chain: n] on: Warning do: [:e | e resume: 0].
  {r. chain}].
ms := Time millisecondsToRun: [
  a := run value: 300.
  b := run value: 600.
  c := run value: 1000].
a first printString , ' once:' , (once value: a last) printString
  , ' all:' , (a last levels asSet = (1 to: 300) asSet) printString
  , '|' , b first displayString , ' once:' , (once value: b last) printString
  , ' innermost:' , (innermost value: b last value: 600) printString
  , '|' , c first displayString , ' one:' , (c last levels size = 1) printString
  , '|fast:' , (ms < 5000) printString
