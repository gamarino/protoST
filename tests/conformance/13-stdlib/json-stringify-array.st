"EXPECT: [1,2,3]"
"§13 stdlib: JSON stringify: of an Array."
m := Import from: 'json'.
m JSON stringify: #(1 2 3).
