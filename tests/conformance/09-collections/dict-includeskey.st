"EXPECT: true"
"§9.8: Dictionary includesKey: tests key presence"
d := Dictionary new.
d at: #two put: 2.
d includesKey: #two.
