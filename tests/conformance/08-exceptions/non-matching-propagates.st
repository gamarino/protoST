"EXPECT: outer"
"§8.3: a non-matching exception propagates to the next enclosing handler"
[ [ Error signal: 'x' ]
    on: Warning
    do: [ :ex | 'inner' ] ]
  on: Error
  do: [ :ex | 'outer' ].
