"EXPECT: caught"
"§13 stdlib: a high surrogate followed by a non-low escape is a parse error."
m := Import from: 'json'.
[ m JSON parse: '"\ud83d\u0041"'. 'no-error' ] on: Error do: [ :e | (e messageText includesSubstring: 'not followed by a low surrogate') ifTrue: [ 'caught' ] ifFalse: [ e messageText ] ].
