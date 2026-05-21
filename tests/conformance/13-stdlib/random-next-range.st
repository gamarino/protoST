"EXPECT: true"
"§13 stdlib: Random>>next yields a Float in [0, 1) across several draws."
m := Import from: 'random'.
r := m Random seed: 99.
ok := true.
1 to: 20 do: [ :i | | v |
  v := r next.
  ((v >= 0) and: [ v < 1 ]) ifFalse: [ ok := false ] ].
ok.
