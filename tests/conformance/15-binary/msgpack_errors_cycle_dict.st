"EXPECT-ERROR: MessagePack: cyclic structure"
"A cycle through a Dictionary value and an OrderedCollection."
| d o |
d := Dictionary new.
o := OrderedCollection new.
o add: d.
d at: 'k' put: o.
MessagePack encode: d.
