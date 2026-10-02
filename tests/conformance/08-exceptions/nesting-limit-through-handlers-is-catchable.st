"EXPECT: stack depth exceeded (nested block evaluation) cleanups:true"
"§8: the engine nesting limit raises an Error that an outer handler catches,
 even when the unwind crosses an on:do: (whose class does not match) and an
 ensure: at every level of a recursion through blocks. Each ensure: block
 runs on the way out. Under MSVC a re-throw inside a catch clause kept every
 frame below it alive, and this unwind overflowed the native stack
 (0xC00000FD) instead of reaching the handler."
Object subclass: #Deep instanceVariableNames: 'cleanups'.
Deep >> init
  cleanups := 0.
Deep >> down: n
  ^ [[#(1) do: [:x | self down: n + 1]] on: ZeroDivide do: [:e | -1]]
      ensure: [cleanups := cleanups + 1].
Deep >> cleanups
  ^ cleanups.
| d r |
d := Deep new init.
r := [d down: 0. 'no error'] on: Error do: [:e | e messageText].
r , ' cleanups:' , (d cleanups > 300) printString.
