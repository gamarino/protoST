"EXPECT: 30"
"§9.8: Dictionary valuesDo: iterates the values"
d := Dictionary new.
d at: #a put: 10.
d at: #b put: 20.
total := 0.
d valuesDo: [ :v | total := total + v ].
total.
