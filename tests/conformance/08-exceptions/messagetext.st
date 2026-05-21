"EXPECT: disk full"
"§8.2: an exception carries messageText set by signal:"
[ Error signal: 'disk full' ]
  on: Error
  do: [ :ex | ex messageText ].
