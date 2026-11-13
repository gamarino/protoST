"EXPECT: true"
"§13 stdlib: \u escapes for BMP code points parse, hex in either case."
m := Import from: 'json'.
(m JSON parse: '"A\u00e9\u00E9\u20AC"') = ('A', 233 asCharacter asString, 233 asCharacter asString, 8364 asCharacter asString).
