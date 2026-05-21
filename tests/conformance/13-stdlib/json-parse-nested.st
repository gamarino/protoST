"EXPECT: Ada"
"§13 stdlib: JSON parse: of a nested object/array structure — read a deep
value back through several levels."
m := Import from: 'json'.
doc := m JSON parse: '{"team": {"members": [{"name": "Ada"}, {"name": "Bob"}]}}'.
((((doc at: 'team') at: 'members') at: 1) at: 'name').
