"EXPECT: 55"
"§8.4: ex return: v makes on:do: yield v"
[ Error signal: 'x'. 999 ]
  on: Error
  do: [ :ex | ex return: 55 ].
