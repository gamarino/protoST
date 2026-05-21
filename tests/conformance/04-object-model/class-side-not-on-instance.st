"EXPECT-ERROR"
"§4.7 + D5 (closed, MNT-b2): a class-side method is NOT reachable from an
instance. Sending a class-side selector to an instance is a doesNotUnderstand.
Class-side methods are stamped `__class_side__`; the engine's SEND dispatch
hides such a method when the receiver is an instance (not a class object)."
Object subclass: #Counter.
Counter class >> classOnly
  ^ 1.
Counter new classOnly.
