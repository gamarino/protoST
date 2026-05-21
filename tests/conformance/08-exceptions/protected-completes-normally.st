"EXPECT: 99"
"§8.3: if the protected block completes normally, on:do: yields its value"
[ 99 ]
  on: Error
  do: [ :ex | 0 ].
