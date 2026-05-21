"EXPECT: caught-as-error"
"§8.1: a subclass instance is caught by a handler guarding the superclass"
Error subclass: #AppError.
[ AppError signal: 'app failure' ]
  on: Error
  do: [ :ex | 'caught-as-error' ].
