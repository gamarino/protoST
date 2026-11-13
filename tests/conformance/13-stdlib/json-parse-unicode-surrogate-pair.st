"EXPECT: true"
"§13 stdlib: a surrogate pair escape is one code point (U+1F600)."
m := Import from: 'json'.
| s | s := m JSON parse: '"\ud83d\ude00"'. (s size = 1) and: [ (s at: 1) asInteger = 128512 ].
