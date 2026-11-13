"EXPECT: caught"
"§13 stdlib: a lone low surrogate is a catchable parse error."
m := Import from: 'json'.
[ m JSON parse: '"\ude00"'. 'no-error' ] on: Error do: [ :e | (e messageText includesSubstring: 'lone low surrogate') ifTrue: [ 'caught' ] ifFalse: [ e messageText ] ].
