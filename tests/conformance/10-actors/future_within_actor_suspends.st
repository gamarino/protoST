"EXPECT: 1000"
"§10 waitWithin: at the top level of an actor method suspends the actor
 instead of blocking its worker; proven without timing. 1000 actors each
 count themselves in an Atom and then waitWithin: 60000 on a gate that only
 the 1000th actor to come in opens. Every actor answers #all. With blocking
 waits this could not happen: the pool's few workers (at most 8, each burying
 at most 17 waiting actors while it helps) would all be held by waiting
 actors long before the 1000th came in, the gate would stay closed, and the
 waits would end in TimeoutErrors after a minute (past the runner's timeout)."
Object subclass: #Entrant.
Entrant >> enter: gate counting: started
  (started swap: [:n | n + 1]) = 1000 ifTrue: [gate resolve: #all].
  ^ gate waitWithin: 60000.

| gate started futures answers |
gate := Future new.
started := Atom on: 0.
futures := (1 to: 1000) collect: [:i | Entrant new asActor enter: gate counting: started].
answers := futures inject: 0 into: [:count :f | f wait = #all ifTrue: [count + 1] ifFalse: [count]].
answers printString
