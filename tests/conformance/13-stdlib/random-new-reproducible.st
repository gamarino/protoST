"EXPECT: true"
"§13 stdlib: Random new uses a fixed default seed, so it is reproducible —
two independently created generators agree."
m := Import from: 'random'.
a := m Random new.
b := m Random new.
(a next) = (b next).
