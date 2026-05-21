"EXPECT: 5"
"§8.4: ex outer continues the handler search outward; the enclosing handler's
resume: 4 then resumes the original signal point, so (4 + 1) = 5. (D7 notes
outer is currently an alias of pass; this test exercises the shared behaviour
that is observable today and does not distinguish strict-outer from pass.)"
[ [ (Warning signal: 'w') + 1 ]
    on: Warning
    do: [ :ex | ex outer ] ]
  on: Warning
  do: [ :ex | ex resume: 4 ].
