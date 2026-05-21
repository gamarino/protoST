"EXPECT-ERROR"
"§8.6: resume: on a non-resumable Error is itself an error"
[ Error signal: 'x' ]
  on: Error
  do: [ :ex | ex resume: 1 ].
