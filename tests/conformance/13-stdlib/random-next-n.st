"EXPECT: 5"
"§13 stdlib: Random>>next: n returns an Array of n successive Floats."
m := Import from: 'random'.
r := m Random seed: 77.
batch := r next: 5.
batch size.
