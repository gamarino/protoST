"EXPECT: caught"
"A call-form send the runtime cannot perform (here, to an actor) signals a
 catchable Error instead of aborting the program."
Object subclass: #C.
C >> twice(n) ^ n * 2.

| c |
c := C new asActor.
[ (c twice(3)) wait ] on: Error do: [ :e | 'caught' ]
