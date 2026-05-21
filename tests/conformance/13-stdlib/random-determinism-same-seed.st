"EXPECT: true"
"§13 stdlib: two Random generators with the same seed produce the same
sequence — the determinism contract."
m := Import from: 'random'.
a := m Random seed: 42.
b := m Random seed: 42.
((a nextInt: 6) = (b nextInt: 6))
  and: [ (a nextInt: 6) = (b nextInt: 6)
  and: [ (a next) = (b next) ] ].
