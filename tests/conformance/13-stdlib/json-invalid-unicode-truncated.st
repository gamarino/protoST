"EXPECT: caught"
"§13 stdlib: a truncated \u escape is a parse error."
m := Import from: 'json'.
[ m JSON parse: '"\u12'. 'no-error' ] on: Error do: [ :e | (e messageText includesSubstring: 'truncated') ifTrue: [ 'caught' ] ifFalse: [ e messageText ] ].
