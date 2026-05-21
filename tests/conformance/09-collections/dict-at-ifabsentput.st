"EXPECT: 88"
"§9.8: Dictionary at:ifAbsentPut: computes, stores, and returns the fallback"
d := Dictionary new.
d at: #k ifAbsentPut: [ 88 ].
d at: #k.
