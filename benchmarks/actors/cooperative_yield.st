"cooperative_yield.st - Actor-model benchmark: cooperative-yield scaling."
""
"One thousand waiter actors are created. Each runs a `work` method whose body"
"sends a message to a shared producer actor and `wait`s on the resulting"
"Future. While a waiter is blocked on that wait the cooperative scheduler"
"PARKS it and frees its worker thread for someone else -- so a thousand"
"in-flight waits are hosted on a pool of just K=2 OS threads."
""
"This is the thing thread-per-actor blocking cannot do: a thousand OS threads"
"would be needed to hold a thousand blocking waits. The harness runs this with"
"PROTOST_WORKERS=2 and reports N (actors) and the wall-clock time."
""
"Result: 1000000  (1000 waiters * producer result 1000)."

Object subclass: #Producer.

Producer >> compute: n
  total := 0.
  1 to: n do: [ :i | total := total + 1 ].
  ^ total.

Object subclass: #Waiter instanceVariableNames: 'prod'.

Waiter >> setProd: anActor
  prod := anActor.
  ^ self.

Waiter >> work
  ^ (prod compute: 1000) wait.

prod := Producer new asActor.

waiters := OrderedCollection new.
1 to: 1000 do: [ :i |
  | w |
  w := Waiter new asActor.
  (w setProd: prod) wait.
  waiters add: w ].

results := OrderedCollection new.
waiters do: [ :w | results add: (w work) ].

sum := 0.
results do: [ :f | sum := sum + f wait ].
sum.
