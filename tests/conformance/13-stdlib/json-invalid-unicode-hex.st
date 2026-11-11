"EXPECT: caught"
"§13 stdlib: a non-hex digit in a \u escape is a parse error."
m := Import from: 'json'.
[ m JSON parse: '"\u00zz"'. 'no-error' ] on: Error do: [ :e | (e messageText includesSubstring: 'invalid hex digit') ifTrue: [ 'caught' ] ifFalse: [ e messageText ] ].
