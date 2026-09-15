"EXPECT: 4"
"§8 + D29: on:do: in a to:do: body, once with a protected block that"
"completes (1) and once with one that signals (handler answers 1)."
s := 0.
1 to: 2 do: [ :i |
  s := s + ([ 1 ] on: Error do: [ :e | 2 ])
         + ([ Error signal: 'boom' ] on: Error do: [ :e | 1 ]) ].
s.
