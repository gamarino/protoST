"EXPECT: 0"
"§9.4: OrderedCollection addFirst: prepends an element"
oc := OrderedCollection new.
oc add: 1.
oc add: 2.
oc addFirst: 0.
oc first.
