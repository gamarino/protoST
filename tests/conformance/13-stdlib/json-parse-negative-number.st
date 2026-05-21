"EXPECT: -17"
"§13 stdlib: JSON parse: handles a leading minus sign."
m := Import from: 'json'.
m JSON parse: '-17'.
