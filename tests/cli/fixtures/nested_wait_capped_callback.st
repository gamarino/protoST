"S40 (review I-3): a worker at the helping cap runs queued tasks (timer
 settles); a timer's thenDo: callback run there can itself wait, nesting a
 second capped wait on the same thread, which must count once. A chain of
 links on one worker; the last one, past the cap, waits (nested) for a Future
 that a timer callback resolves with a reply it waits for from another actor.
 The length comes from the command line (default 20); the answer is
 length - 1 + 5."
Object subclass: #Srv.
Srv >> ping
  ^ 5.
Object subclass: #Link instanceVariableNames: 'next srv'.
Link >> next: n srv: s
  next := n. srv := s.
Link >> ping
  | f |
  next isNil ifFalse: [#(1) do: [:i | ^ 1 + (next ping wait)]].
  f := Future new.
  (Future after: 1) thenDo: [:x | f resolve: srv ping wait].
  #(1) do: [:i | ^ f wait].
  ^ 0.

| n srv links |
n := Smalltalk arguments isEmpty ifTrue: [20] ifFalse: [Smalltalk arguments first asNumber].
srv := Srv new asActor.
links := (1 to: n) collect: [:i | Link new asActor].
1 to: n do: [:i | ((links at: i) next: (i < n ifTrue: [links at: i + 1] ifFalse: [nil]) srv: srv) wait].
(links first ping wait) printString
