"EXPECT: true"
"§13 stdlib: JSON parse: of a number with an exponent yields a Float."
m := Import from: 'json'.
(m JSON parse: '1.5e2') = 150.0.
