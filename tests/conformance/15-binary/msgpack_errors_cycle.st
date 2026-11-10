"EXPECT-ERROR: MessagePack: cyclic structure"
"An Array that contains itself signals Error; it does not overflow the stack."
| a |
a := Array new: 1.
a at: 1 put: a.
MessagePack encode: a.
