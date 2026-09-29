"EXPECT: #(1 'deadlock')"
"Two actors that wait on each other can never proceed (an actor that waits
 handles no other message). The cycle is detected when the second wait would
 close it, and signalled as an Error instead of hanging for ever."
Object subclass: #Pinger instanceVariableNames: 'peer'.
Pinger >> peer: anActor  peer := anActor.
Pinger >> ping: n
  n = 0 ifTrue: [^ 0].
  ^ 1 + (peer ping: n - 1) wait.

| a b out |
a := Pinger new asActor.
b := Pinger new asActor.
(a peer: b) wait.
(b peer: a) wait.
out := OrderedCollection new.
out add: (a ping: 1) wait.
out add: ([(a ping: 2) wait] on: Error do: [:e |
  (e messageText includesSubstring: 'deadlock') ifTrue: ['deadlock'] ifFalse: [e messageText]]).
out asArray
