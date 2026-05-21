"EXPECT: from-instance"
"§8.2: signalling an existing exception instance"
e := Error new.
[ e signal ]
  on: Error
  do: [ :ex | 'from-instance' ].
