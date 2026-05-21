"EXPECT: bare"
"§8.2: Error signal raises a bare Error with no message text"
[ Error signal ]
  on: Error
  do: [ :ex | 'bare' ].
