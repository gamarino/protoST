"EXPECT: 3"
"§9.7: Bag size counts occurrences"
b := Bag new.
b add: #x withOccurrences: 2.
b add: #y.
b size.
