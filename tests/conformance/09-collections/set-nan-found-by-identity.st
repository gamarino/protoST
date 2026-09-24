"EXPECT: true"
"§9.6: the very NaN object a Set holds is still found, by identity (D31/D32)"
n := Float nan.
s := Set new.
s add: n.
s includes: n.
