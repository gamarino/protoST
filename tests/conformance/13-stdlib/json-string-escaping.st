"EXPECT: true"
"§13 stdlib: a string containing a quote and a newline survives a
stringify: / parse: round-trip unchanged."
m := Import from: 'json'.
original := 'a"b', (10 asCharacter), 'c'.
recovered := m JSON parse: (m JSON stringify: original).
recovered = original.
