"EXPECT: 42"
"§13 stdlib: JSON parse: of a flat object yields a Dictionary keyed by string."
m := Import from: 'json'.
d := m JSON parse: '{"answer": 42, "name": "Ada"}'.
d at: 'answer'.
