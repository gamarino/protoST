"EXPECT: warned"
"§8.3: on:do:on:do: registers two guard/handler pairs"
[ Warning signal: 'w' ]
  on: Error
  do: [ :ex | 'errored' ]
  on: Warning
  do: [ :ex | 'warned' ].
