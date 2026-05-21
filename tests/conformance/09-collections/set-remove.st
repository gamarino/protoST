"EXPECT: false"
"§9.6: Set remove: drops an element"
s := Set new.
s add: 5.
s remove: 5.
s includes: 5.
