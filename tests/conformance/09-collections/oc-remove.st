"EXPECT: 2"
"§9.4: OrderedCollection remove: removes a matching element"
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
oc remove: 2.
oc size.
