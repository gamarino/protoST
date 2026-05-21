"EXPECT: true"
"-- 03_random.st ----------------------------------------------------------"
"-- The `random` standard-library module: a seedable, deterministic PRNG.   --"
"-- Two generators created with the same seed produce the identical         --"
"-- sequence — the determinism contract that makes simulations              --"
"-- reproducible. `nextInt:` answers an integer in 1..n.                    --"
"-- Expected output: true                                                   --"

m := Import from: 'random'.

a := m Random seed: 2026.
b := m Random seed: 2026.

"Draw ten dice rolls from each; the sequences must agree element-by-element."
identical := true.
1 to: 10 do: [ :i |
  ((a nextInt: 6) = (b nextInt: 6)) ifFalse: [ identical := false ] ].

identical.
