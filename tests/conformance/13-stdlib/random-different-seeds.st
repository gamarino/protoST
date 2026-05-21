"EXPECT: false"
"§13 stdlib: two Random generators with different seeds produce different
sequences."
m := Import from: 'random'.
a := m Random seed: 1.
b := m Random seed: 2.
(a next) = (b next).
