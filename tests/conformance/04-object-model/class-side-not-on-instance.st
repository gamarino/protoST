"EXPECT-ERROR: doesNotUnderstand: classOnly"
"§4.7 + D5 (closed, MNT-b2): a class-side method is NOT reachable from an
instance. Sending a class-side selector to an instance is a doesNotUnderstand.
Since D52 a class-side method lives on the class's metaclass, which is not on
an instance's lookup chain."
Object subclass: #Counter.
Counter class >> classOnly
  ^ 1.
Counter new classOnly.
