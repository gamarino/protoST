"XFAIL: EXPECT: caught"
"§5.2 + D3: doesNotUnderstand is intended to be a catchable message; spec-correct
behaviour is that an on: Error do: handler catches it. Currently it is a hard
failure that escapes the handler."
[ 3 frobnicate. 'not reached' ]
  on: Error
  do: [ :ex | 'caught' ].
