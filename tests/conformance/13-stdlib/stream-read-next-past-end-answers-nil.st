"EXPECT: #(1 2 nil true)"
"§13 stdlib: ReadStream next past the end answers nil, as in Pharo; atEnd
 tells the end apart from a nil element."
| rs |
rs := ReadStream on: #(1 2).
{ rs next. rs next. rs next. rs atEnd }
