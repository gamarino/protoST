"EXPECT: 7"
"§8.4: a handler that falls off its end — on:do: yields its last value"
[ Error signal: 'x'. 999 ]
  on: Error
  do: [ :ex | 3 + 4 ].
