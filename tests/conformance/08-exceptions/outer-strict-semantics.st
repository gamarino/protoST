"EXPECT: 4"
"§8.4: ex outer continues the handler search outward; the enclosing handler's
resume: 4 answers 4 as the value of ex outer inside the inner handler, which
falls off its end with it, so the inner on:do: answers 4. The protected block
does not continue at the signal: (signal + 1) is never computed. (Until D7 was
fixed, outer behaved as pass and this test expected 5, the value of a
resumption at the signal.)"
[ [ (Warning signal: 'w') + 1 ]
    on: Warning
    do: [ :ex | ex outer ] ]
  on: Warning
  do: [ :ex | ex resume: 4 ].
