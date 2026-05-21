"EXPECT: 2"
"§9.4: removeFirst shrinks the collection"
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
oc removeFirst.
oc size.
