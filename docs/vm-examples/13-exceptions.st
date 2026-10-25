| r |
r := [ Error signal: 'boom'. 'not reached' ]
  on: Error
  do: [ :e | e messageText ].
r printNl.
[ 'body' printNl ] ensure: [ 'cleanup' printNl ].
