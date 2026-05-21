"EXPECT: handled"
"§8.3: on:do: catches a matching signalled exception"
[ Error signal: 'boom'. 'not reached' ]
  on: Error
  do: [ :ex | 'handled' ].
