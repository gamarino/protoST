"EXPECT: outer-handled"
"§8.4: ex pass resumes the handler search outward"
[ [ Error signal: 'x' ]
    on: Error
    do: [ :ex | ex pass ] ]
  on: Error
  do: [ :ex | 'outer-handled' ].
