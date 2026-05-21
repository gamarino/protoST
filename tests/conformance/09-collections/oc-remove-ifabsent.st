"EXPECT: missing"
"§9.4: OrderedCollection remove:ifAbsent: runs the fallback block when absent"
oc := OrderedCollection new.
oc add: 1.
oc remove: 99 ifAbsent: [ 'missing' ].
