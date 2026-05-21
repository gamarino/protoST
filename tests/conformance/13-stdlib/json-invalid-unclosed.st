"EXPECT: caught"
"§13 stdlib: invalid JSON ({bad) signals a catchable Error."
m := Import from: 'json'.
[ m JSON parse: '{bad'. 'no-error' ] on: Error do: [ :e | 'caught' ].
