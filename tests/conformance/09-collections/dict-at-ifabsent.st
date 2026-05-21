"EXPECT: 0"
"§9.8: Dictionary at:ifAbsent: returns the fallback for an absent key"
d := Dictionary new.
d at: #three ifAbsent: [ 0 ].
