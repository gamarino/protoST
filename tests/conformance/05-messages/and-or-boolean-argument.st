"EXPECT: true"
"and: / or: accept a Boolean argument as well as a block, as in Pharo."
((true and: false) = false) & ((false or: true) = true) & ((true and: [true]) = true) & ((false or: [false]) = false).
