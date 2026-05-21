"EXPECT: 3"
"§3.9: a cascade sends several messages to the same receiver;
its value is the last message's value (size after three adds)"
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
oc size.
