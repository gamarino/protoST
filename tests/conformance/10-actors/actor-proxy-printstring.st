"EXPECT: a Thing"
"§10.1 / §10.2: the actor proxy is fully transparent — EVERY message, even
printString, is forwarded asynchronously and returns a Future. Waiting on it
yields the wrapped object's printString. There is no synchronous way to observe
'this is an Actor' from the proxy; that opacity is by design."
Object subclass: #Thing.
(Thing new asActor printString) wait.
