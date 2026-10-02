"EXPECT: level 498 handled:498"
"§8: a handler block that signals a new Error hands it to the next outer
 handler, which runs on top of it. 498 levels of that nest 498 handler blocks
 inside 498 protected blocks (nearly 1,000 engines, the deepest such chain below the
 nesting limit of 1,000; S23 covers what happens beyond it); the outermost
 handler then falls off its end, and that unwind leaves through every handler
 block and signal frame on the way down. Under MSVC a re-throw
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
m := [r relay: 498] on: Error do: [:e | e messageText].
m , ' handled:' , r handled printString.
