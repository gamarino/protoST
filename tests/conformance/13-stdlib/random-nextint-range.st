"EXPECT: true"
"§13 stdlib: Random>>nextInt: 6 stays within 1..6 across many draws."
m := Import from: 'random'.
r := m Random seed: 12345.
ok := true.
1 to: 50 do: [ :i | | v |
  v := r nextInt: 6.
  ((v >= 1) and: [ v <= 6 ]) ifFalse: [ ok := false ] ].
ok.
