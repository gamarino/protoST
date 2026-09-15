"EXPECT: 42"
"S13: an actor suspended in `wait` resumes only once the awaited future has"
"settled. The Waiter actor waits on a future the main thread resolves later;"
"meanwhile a second message is sent to it, which wakes the scheduler for the"
"actor. That wakeup must not resume the suspended method (the future is still"
"pending): the message stays queued until the method completes. Before the"
"fix the method resumed with nil (`nil + 1` -> doesNotUnderstand)."
Object subclass: #Waiter.
Waiter >> waitOn: aFuture  ^ aFuture wait + 1.
Waiter >> ping  ^ 7.
w := Waiter new asActor.
f := Future new.
r := w waitOn: f.
p := w ping.
0 sleep: 200.
f resolve: 41.
(r wait) + ((p wait) - 7).
