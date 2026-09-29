"EXPECT: a Thing (actor)"
"§10.1 / §10.2: printing is a message about the reference, answered by the
 proxy synchronously without running the object's printOn: on another thread;
 every other message is forwarded asynchronously and answers a Future."
Object subclass: #Thing.
Thing new asActor printString
