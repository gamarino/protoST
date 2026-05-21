"EXPECT: true"
"§13 stdlib: Random>>between:and: stays within the inclusive range."
m := Import from: 'random'.
r := m Random seed: 2026.
ok := true.
1 to: 50 do: [ :i | | v |
  v := r between: 10 and: 20.
  ((v >= 10) and: [ v <= 20 ]) ifFalse: [ ok := false ] ].
ok.
