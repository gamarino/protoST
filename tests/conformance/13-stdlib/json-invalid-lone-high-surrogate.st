"EXPECT: caught"
"§13 stdlib: a lone high surrogate is a catchable parse error."
m := Import from: 'json'.
[ m JSON parse: '"\ud83dx"'. 'no-error' ] on: Error do: [ :e | (e messageText includesSubstring: 'lone high surrogate') ifTrue: [ 'caught' ] ifFalse: [ e messageText ] ].
