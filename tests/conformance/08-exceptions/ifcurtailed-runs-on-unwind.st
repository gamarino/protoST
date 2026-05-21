"EXPECT: 1"
"§8.5: ifCurtailed: cleanup runs on an abnormal exit"
flag := 0.
[ [ Error signal: 'x' ] ifCurtailed: [ flag := 1 ] ]
  on: Error
  do: [ :ex | nil ].
flag.
