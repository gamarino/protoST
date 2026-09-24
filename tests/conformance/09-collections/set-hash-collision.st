"EXPECT: 2"
"§9.6: two keys that collide on the hash stay two elements (D32 bucket handling)"
"2^60 and 2^60+1 are distinct integers that round to the same double, which is"
"what the numeric key hash folds, so they share a slot."
a := 1.
1 to: 60 do: [ :i | a := a * 2 ].
b := a + 1.
s := Set new.
s add: a.
s add: b.
s size.
