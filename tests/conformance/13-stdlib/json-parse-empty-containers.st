"EXPECT: 0"
"§13 stdlib: JSON parse: of an empty object and an empty array."
m := Import from: 'json'.
obj := m JSON parse: '{}'.
arr := m JSON parse: '[]'.
obj size + arr size.
