"EXPECT: 30"
"-- 01_async_and_wait.st ---------------------------------------------------"
"-- Actor basics. `asActor` wraps an object as an actor proxy; a message    --"
"-- sent to the proxy is enqueued and a Future is returned IMMEDIATELY.     --"
"-- `wait` blocks until the Future settles and yields the value. The actor --"
"-- processes its mailbox one message at a time, so its state is           --"
"-- serialised without the programmer writing a lock.                       --"
"-- Expected output: 30                                                     --"

Object subclass: #Counter instanceVariableNames: 'value'.
Counter >> initialize
  value := 0.
  ^ self.
Counter >> add: n
  value := value + n.
  ^ value.
Counter >> value
  ^ value.

base := Counter new.
base initialize.
actor := base asActor.

"These three sends return Futures; the actor runs them in mailbox order."
actor add: 5.
actor add: 10.
actor add: 15.

"wait on the final read — the actor has processed every queued message."
(actor value) wait.
