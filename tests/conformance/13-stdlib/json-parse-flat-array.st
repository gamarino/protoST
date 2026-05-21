"EXPECT: 6"
"§13 stdlib: JSON parse: of a flat array yields an indexable Array."
m := Import from: 'json'.
a := m JSON parse: '[1, 2, 3]'.
(a at: 1) + (a at: 2) + (a at: 3).
