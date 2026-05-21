"EXPECT: caught"
"§5.2 + D3 (closed, MNT-b2): an unresolved selector signals a catchable
MessageNotUnderstood (a subclass of Error), so an `on: Error do:` handler
catches it. It used to be a hard failure that escaped the handler."
[ 3 frobnicate. 'not reached' ]
  on: Error
  do: [ :ex | 'caught' ].
