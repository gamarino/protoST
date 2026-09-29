"EXPECT: Future rejected: doesNotUnderstand: fooBarBaz (receiver class: W)"
"An unknown selector sent to an actor rejects its Future with the same
 doesNotUnderstand: text a synchronous send reports."
Object subclass: #W.
a := W new asActor.
[(a fooBarBaz) wait] on: Error do: [:e | e messageText].
