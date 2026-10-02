"EXPECT: 300:level 300 once:true limit:false 499:level 499 once:true limit:true 500:level 500 once:true limit:true 3000:level 3000 once:true limit:true fast:true"
"§8: a chain of handlers that each signal a new Error stops at the engine
 nesting limit (S23). Each level nests a protected block, and every handler
 runs on top of the signal that reached it, so about 499 levels exhaust the
 1,000 nested engines. The handler that can no longer start receives the
 documented catchable Error instead ('stack depth exceeded (nested block
 evaluation)'), once the stack has unwound to its on:do:; the chain then
 goes on outward. Every handler runs at most once, and the program finishes
 promptly. Before the fix each handler whose re-signal hit the limit was
 entered again by the translation of the limit error at every primitive
 boundary below it, so the work doubled per level and the program did not
 finish. 3000 levels also exhaust the limit in the recursion itself."
Object subclass: #Relay instanceVariableNames: 'seen twice limit'.
Relay >> init: size
  seen := Array new: size.
  twice := false.
  limit := 0.
Relay >> relay: n
  n = 0 ifTrue: [^ Error signal: 'bottom'].
  ^ [self relay: n - 1]
      on: Error
      do: [:e |
        (seen at: n) isNil ifFalse: [twice := true].
        seen at: n put: true.
        e messageText = 'stack depth exceeded (nested block evaluation)'
          ifTrue: [limit := limit + 1].
        Error signal: 'level ' , n printString].
Relay >> report
  ^ ' once:' , twice not printString , ' limit:' , (limit > 0) printString.
| run out ms |
run := [:n | | r m |
  r := Relay new init: n.
  m := [r relay: n] on: Error do: [:e | e messageText].
  n printString , ':' , m , r report].
out := ''.
ms := Time millisecondsToRun: [
  #(300 499 500 3000) do: [:n |
    out := out , (out isEmpty ifTrue: [''] ifFalse: [' ']) , (run value: n)]].
out , ' fast:' , (ms < 5000) printString.
