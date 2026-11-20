"S43: a wait past the helping cap parks until an event it can act on; it
 does not poll. With one configured worker, a chain of 18 links, each waiting
 inside a block for the next, buries 17 actors on that worker, so the last
 link's wait, for a Future a timer settles after the delay given on the
 command line (ms), is at the cap. The answer is 18."
Object subclass: #Link instanceVariableNames: 'next'.
Link >> next: n
  next := n.
Link >> ping
  next isNil ifTrue: [
    #(1) do: [:i | (Future after: (Smalltalk arguments at: 1) asNumber) wait].
    ^ 1].
  #(1) do: [:i | ^ 1 + (next ping wait)].
  ^ 0.

| links |
links := (1 to: 18) collect: [:i | Link new asActor].
1 to: 17 do: [:i | ((links at: i) next: (links at: i + 1)) wait].
(links first ping wait) printString
