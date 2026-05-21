"EXPECT: 7"
"§13 stdlib: JSON stringify: of a Dictionary produces a parseable JSON object."
m := Import from: 'json'.
d := Dictionary new.
d at: 'n' put: 7.
(m JSON parse: (m JSON stringify: d)) at: 'n'.
