"EXPECT: caught"
"§9.1: an out-of-range index Error is catchable by on:do:"
[ #(1 2 3) at: 99 ]
  on: Error
  do: [ :ex | 'caught' ].
