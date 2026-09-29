"EXPECT: 24000"
"Many actors blocked in accept at once: the worker pool grows while they wait
 (its lock is never held across an allocation) and collections keep running."
Import from: 'net'.
Object subclass: #Waiter.
Waiter >> go: srv
  | junk |
  1 to: 20 do: [:i |
    junk := (1 to: 200) collect: [:k | k printString , 'x'].
    srv acceptTimeout: 1].
  ^ junk size.
servers := (1 to: 120) collect: [:i | ServerSocket listenOn: 0 host: '127.0.0.1'].
futures := servers collect: [:s | Waiter new asActor go: s].
total := 0.
futures do: [:f | total := total + f wait].
servers do: [:s | s close].
total printString.
