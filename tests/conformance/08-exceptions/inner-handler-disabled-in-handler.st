"EXPECT: outer"
"§8.3: a signal inside a handler is caught by an OUTER handler, never itself"
[ [ Error signal: 'first' ]
    on: Error
    do: [ :ex | Error signal: 'second' ] ]
  on: Error
  do: [ :ex | 'outer' ].
