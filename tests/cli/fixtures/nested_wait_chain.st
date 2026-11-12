"S40: a chain of actors, each waiting inside a block (so the wait cannot
 suspend the actor and blocks its worker, running other actors' messages
 meanwhile) for the next one. With one worker the nesting passes the helping
 cap (16 waits on one thread) at the 18th link; the chain must still finish.
 The length comes from the command line (default 200); the answer is the
 length."
Object subclass: #Link instanceVariableNames: 'next'.
Link >> next: n
  next := n.
Link >> ping
  next isNil ifTrue: [^ 1].
  #(1) do: [:i | ^ 1 + (next ping wait)].
  ^ 0.

| n links |
n := Smalltalk arguments isEmpty ifTrue: [200] ifFalse: [Smalltalk arguments first asNumber].
links := (1 to: n) collect: [:i | Link new asActor].
1 to: n - 1 do: [:i | ((links at: i) next: (links at: i + 1)) wait].
(links first ping wait) printString
