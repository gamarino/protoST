"EXPECT: done"
"§8.4: ex resume: continues the protected computation past the signal"
[ Warning signal: 'low ink'. 'done' ]
  on: Warning
  do: [ :ex | ex resume: nil ].
