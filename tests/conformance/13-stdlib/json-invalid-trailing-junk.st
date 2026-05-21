"EXPECT: caught"
"§13 stdlib: trailing non-whitespace after a complete value signals a
catchable Error."
m := Import from: 'json'.
[ m JSON parse: '42 garbage'. 'no-error' ] on: Error do: [ :e | 'caught' ].
