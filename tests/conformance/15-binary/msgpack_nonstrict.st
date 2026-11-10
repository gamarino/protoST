"EXPECT: 2 true 2 1"
"strictKeys: false decodes a map with an integer key, into a Dictionary and, ordered, into a MessagePackMap."
| d m |
d := MessagePack decode: #[16r81 1 2] strictKeys: false.
m := MessagePack decodeOrdered: #[16r81 1 2] strictKeys: false.
(d at: 1) printString, ' ', (m isKindOf: MessagePackMap) printString, ' ', (m associations first value) printString, ' ', d size printString
