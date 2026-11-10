"EXPECT: 200 true"
"§10 Timeouts inside actors: 200 actors each waitWithin: 100 on a Future that
 never settles. Every wait ends with a TimeoutError (the message Future is
 rejected with it) and the whole run takes under 5 s, a generous upper bound.
 That waits suspend rather than block is proven, without timing, by
 future_within_actor_suspends.st."
Object subclass: #Waiter.
Waiter >> await: aFuture
  ^ aFuture waitWithin: 100.

| never futures timeouts ms |
never := Future new.
ms := Time millisecondsToRun: [
  futures := (1 to: 200) collect: [:i | Waiter new asActor await: never].
  timeouts := 0.
  futures do: [:f |
    [f wait] on: TimeoutError do: [:e | timeouts := timeouts + 1]]].
timeouts printString , ' ' , (ms < 5000) printString
