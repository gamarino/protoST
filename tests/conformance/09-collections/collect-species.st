"EXPECT: 3"
"§9.2: collect: produces a collection of the receiver's species (OC from OC)"
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
(oc collect: [ :e | e + 100 ]) size.
