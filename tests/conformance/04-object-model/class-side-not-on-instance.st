"XFAIL: EXPECT-ERROR"
"§4.7 + D5: a class-side method should NOT be reachable from an instance.
Spec-correct: sending a class-side selector to an instance is a doesNotUnderstand.
Currently class-side methods install on the same prototype, so this succeeds."
Object subclass: #Counter.
Counter class >> classOnly
  ^ 1.
Counter new classOnly.
