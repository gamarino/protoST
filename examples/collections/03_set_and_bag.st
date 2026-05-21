"EXPECT: true"
"-- 03_set_and_bag.st ------------------------------------------------------"
"-- Two hashed collections contrasted. A Set deduplicates: adding a value   --"
"-- already present is a no-op. A Bag counts: it records how many times     --"
"-- each value was added. Adding the same five words to each, the Set has   --"
"-- the distinct count and the Bag the total count.                         --"
"-- Expected output: true                                                   --"

words := #(apple pear apple plum apple).

set := Set new.
words do: [ :w | set add: w ].

bag := Bag new.
words do: [ :w | bag add: w ].

"Set holds 3 distinct words; Bag holds 5 occurrences, 3 of them 'apple'."
((set size = 3) and: [ bag size = 5 ])
  and: [ (bag occurrencesOf: #apple) = 3 ].
