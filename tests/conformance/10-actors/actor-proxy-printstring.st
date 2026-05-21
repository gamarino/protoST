"EXPECT: a Thing"
"§10.2: every message sent to an actor proxy is asynchronous — even printString
returns a Future, which resolves to the wrapped object's printString."
Object subclass: #Thing.
(Thing new asActor printString) wait.
