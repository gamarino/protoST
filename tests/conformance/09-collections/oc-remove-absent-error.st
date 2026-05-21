"EXPECT-ERROR"
"§9.4: OrderedCollection remove: signals an Error when the element is absent"
oc := OrderedCollection new.
oc add: 1.
oc remove: 99.
