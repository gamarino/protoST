"EXPECT: 1"
"§8.5: ensure: cleanup runs even when the block is abandoned by an unwind"
flag := 0.
[ [ Error signal: 'x' ] ensure: [ flag := 1 ] ]
  on: Error
  do: [ :ex | nil ].
flag.
