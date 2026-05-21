"EXPECT: caught"
"§13 stdlib: ReadStream next past the end signals a catchable Error."
m := Import from: 'stream'.
rs := m ReadStream on: #(1 2).
rs next.
rs next.
[ rs next. 'no-error' ] on: Error do: [ :ex | 'caught' ].
