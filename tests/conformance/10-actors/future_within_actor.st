"EXPECT: 200 200 true"
"§10 waitWithin: inside an actor method suspends the actor, as wait does,
 instead of blocking its worker. 200 actors each waitWithin: 100 on a Future
 that never settles; every one times out (its message Future is rejected
 with TimeoutError) and the whole run takes under 5 s.

 Two observations, both from the program's own state:
 - the count of TimeoutErrors (200);
 - how many actors had entered their method when each timeout was seen:
   every one reports 200 (the minimum over all of them), since all 200 waits
   are in progress at once. A worker that blocked in the wait would run at
   most a few actors at a time (a helping worker buries at most 17), so the
   first timeout would see far fewer. The bound this relies on is that 200
   trivial message dispatches start within the 100 ms deadline.
 The wall-time bound (5 s) is the generous upper bound of the plan; on a
 host with eight workers, blocking waits would take at least 2.5 s."
Object subclass: #Waiter.
Waiter >> await: aFuture counting: started
  started swap: [:n | n + 1].
  ^ aFuture waitWithin: 100.

| never started minSeen futures timeouts ms |
never := Future new.
started := Atom on: 0.
minSeen := Atom on: 1000.
ms := Time millisecondsToRun: [
  futures := (1 to: 200) collect: [:i | Waiter new asActor await: never counting: started].
  futures do: [:f |
    f catch: [:e | | seen | seen := started value.
      minSeen swap: [:m | m min: seen]]].
  timeouts := 0.
  futures do: [:f |
    [f wait] on: TimeoutError do: [:e | timeouts := timeouts + 1]]].
timeouts printString , ' ' , minSeen value printString , ' ' , (ms < 5000) printString
