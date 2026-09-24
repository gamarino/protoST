"EXPECT: true"
"§9.6-9.8: all three hashed collections now agree on element equality (D32)"
s := Set new.  s add: 1.
b := Bag new.  b add: 1.
d := Dictionary new. d at: 1 put: 'v'.
((s includes: 1.0) and: [ b includes: 1.0 ]) and: [ d includesKey: 1.0 ].
