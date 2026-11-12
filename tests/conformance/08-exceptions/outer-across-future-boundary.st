"EXPECT: #('nil in actor' 'caller: actor failure' 'outer: actor failure')"
"§8.4 / §10: handlers belong to the thread that runs the code, so they do not
 span an actor message. Inside an actor method, ex outer finds no enclosing
 handler in the method, even when the caller waits inside an on:do: for the
 same class: a Warning's default action answers nil there, and an Error ends
 the message, whose Future is rejected. Waiting re-signals the Error in the
 caller, where outer reaches the caller's enclosing handler."
Object subclass: #OuterWorker.
OuterWorker >> warn
  ^ [Warning signal: 'w'. 'body'] on: Warning do: [:e | e outer printString , ' in actor'].
OuterWorker >> fail
  ^ [Error signal: 'actor failure'] on: Error do: [:e | e outer. 'unreached'].

| w out |
w := OuterWorker new asActor.
out := OrderedCollection new.
out add: ([w warn wait] on: Warning do: [:e | 'caller saw the warning']).
out add: ([w fail wait] on: Error do: [:e | 'caller: ' , e messageText]).
out add: ([[w fail wait] on: Error do: [:e | e outer. 'unreached']]
            on: Error do: [:e | 'outer: ' , e messageText]).
out asArray
