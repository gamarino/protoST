"EXPECT: level 450 handled:450"
"§8: a handler block that signals a new Error hands it to the next outer
 handler, which runs on top of it. 450 levels of that nest 450 handler blocks
 inside 450 signal frames (about 900 engines, below the nesting limit); the
 outermost handler then falls off its end, and that unwind leaves through
 every handler block and signal frame on the way down. Under MSVC a re-throw
 inside the signal frame's catch clause kept every frame below it alive."
Object subclass: #Relay instanceVariableNames: 'handled'.
Relay >> init
  handled := 0.
Relay >> relay: n
  n = 0 ifTrue: [^ Error signal: 'bottom'].
  ^ [self relay: n - 1]
      on: Error
      do: [:e | handled := handled + 1. Error signal: 'level ' , n printString].
Relay >> handled
  ^ handled.
| r m |
r := Relay new init.
m := [r relay: 450] on: Error do: [:e | e messageText].
m , ' handled:' , r handled printString.
