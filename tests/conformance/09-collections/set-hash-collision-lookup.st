"EXPECT: true"
"§9.6: both keys of a hash collision remain findable (D32 bucket handling)"
a := 1.
1 to: 60 do: [ :i | a := a * 2 ].
b := a + 1.
s := Set new.
s add: a.
s add: b.
(s includes: a) and: [ s includes: b ].
