"EXPECT: true"
"§9.6: a Set holding an integer includes the equal Float (D32)"
s := Set new.
s add: 1.
s includes: 1.0.
