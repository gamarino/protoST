"EXPECT: 3"
"§8.4: ex retry re-evaluates the protected block from the start"
attempts := 0.
[ attempts := attempts + 1.
  (attempts < 3) ifTrue: [ Error signal: 'again' ].
  attempts ]
  on: Error
  do: [ :ex | ex retry ].
