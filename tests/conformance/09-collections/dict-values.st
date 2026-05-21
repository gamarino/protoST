"EXPECT: 2"
"§9.8: Dictionary values returns a collection of the values"
d := Dictionary new.
d at: #a put: 10.
d at: #b put: 20.
d values size.
