"EXPECT: false"
"§9.8: Dictionary removeKey: removes a key"
d := Dictionary new.
d at: #k put: 1.
d removeKey: #k.
d includesKey: #k.
