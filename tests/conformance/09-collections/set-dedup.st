"EXPECT: 2"
"§9.6: a Set deduplicates — adding a present element is a no-op"
s := Set new.
s add: 1.
s add: 2.
s add: 1.
s size.
