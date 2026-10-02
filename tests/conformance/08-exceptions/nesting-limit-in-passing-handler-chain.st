"EXPECT: pass:bottom/stack depth exceeded (nested block evaluation) outer:bottom/stack depth exceeded (nested block evaluation) signal:bottom/stack depth exceeded (nested block evaluation) signal::again/again once:true fast:true"
"§8: handlers that hand the exception outward with pass, outer, signal or
 signal: (S23). Below the engine nesting limit (300 levels) the outermost
 handler receives the original exception. At 1,000 levels the recursion
 itself reaches the limit: the limit's documented catchable Error goes to the
 innermost handler, each handler hands it on, and the outermost handler
 answers its messageText (signal: replaces that text with 'again'). No
 handler runs twice, and the program finishes promptly; before the fix it
 did not finish."
Object subclass: #Chain instanceVariableNames: 'action runs'.
Chain >> init: aSymbol
  action := aSymbol.
  runs := 0.
Chain >> chain: n
  n = 0 ifTrue: [^ Error signal: 'bottom'].
  ^ [self chain: n - 1]
      on: Error
      do: [:e |
        runs := runs + 1.
        action = #pass ifTrue: [e pass].
        action = #outer ifTrue: [e outer].
        action = #signal ifTrue: [e signal].
        e signal: 'again'].
Chain >> runs
  ^ runs.
| once at out ms |
once := true.
at := [:sym :n | | c m |
  c := Chain new init: sym.
  m := [c chain: n] on: Error do: [:e | e messageText].
  c runs > n ifTrue: [once := false].
  m].
out := ''.
ms := Time millisecondsToRun: [
  #(#pass #outer #signal #signal:) do: [:sym |
    out := out , (out isEmpty ifTrue: [''] ifFalse: [' ']) , sym , ':'
      , (at value: sym value: 300) , '/' , (at value: sym value: 1000)]].
out , ' once:' , once printString , ' fast:' , (ms < 5000) printString.
