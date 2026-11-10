"EXPECT: 200"
"§10 waitWithin: nested in a block of an actor method (here inside ensure:)
 cannot suspend the actor, so it blocks its worker, which helps by running
 other actors' messages meanwhile, up to a nesting cap. With 200 such actors
 every worker reaches the cap; the timeouts must still fire and every wait
 must end with a TimeoutError. A worker past the cap still runs the queued
 timer settles (waitHelping), so the program ends instead of waiting for
 ever; the runner's own timeout would report a hang."
Object subclass: #NestedWaiter.
NestedWaiter >> await: aFuture
  ^ [aFuture waitWithin: 100] ensure: [nil].

| never futures timeouts |
never := Future new.
futures := (1 to: 200) collect: [:i | NestedWaiter new asActor await: never].
timeouts := 0.
futures do: [:f |
  [f wait] on: TimeoutError do: [:e | timeouts := timeouts + 1]].
timeouts printString
