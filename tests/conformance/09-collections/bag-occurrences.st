"EXPECT: 3"
"§9.7: a Bag counts how many times an element was added"
b := Bag new.
b add: #x.
b add: #x.
b add: #x.
b occurrencesOf: #x.
