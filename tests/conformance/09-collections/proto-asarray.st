"EXPECT: 3"
"§9.2: asArray returns an Array copy of the receiver"
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
oc asArray size.
