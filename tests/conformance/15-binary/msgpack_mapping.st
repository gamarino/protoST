"EXPECT: failed: #()"
"The Smalltalk to MessagePack mapping, as a round trip: nil and booleans, small and 2^63 and 2^64-1 and -2^63 integers, a Float (bit-exact NaN and -0.0), a String with NUL, non-ASCII text, a ByteArray, an Array and an OrderedCollection (both come back as an Array), a Dictionary (by contents), a MessagePackMap (order kept, bytes exact against msgpack-python's), an Ext, nested containers, shared (not cyclic) substructure, a Symbol (comes back as a String), and decodeFirst:."
r := OrderedCollection new.
rt := [:x | MessagePack decode: (MessagePack encode: x)].
hexOf := [:f | | w | w := WriteStream on: ByteArray new. w nextPutFloat64: f bigEndian: true. w contents hex].
r add: ((rt value: nil) isNil and: [(rt value: true) == true and: [(rt value: false) == false]]).
r add: ((#(0 1 127 128 -1 -32 -33 65536 4294967296) collect: [:x | rt value: x]) = #(0 1 127 128 -1 -32 -33 65536 4294967296)).
r add: (((2 raisedTo: 63) = (rt value: (2 raisedTo: 63))) and: [
  ((2 raisedTo: 64) - 1) = (rt value: (2 raisedTo: 64) - 1) and: [
  (2 raisedTo: 63) negated = (rt value: (2 raisedTo: 63) negated)]]).
r add: ((MessagePack encode: (2 raisedTo: 63)) = #[16rCF 16r80 0 0 0 0 0 0 0]).
r add: ((MessagePack encode: 1.5) = #[16rCB 16r3F 16rF8 0 0 0 0 0 0]).
b := (rt value: Float nan).
r add: (b isFloat and: [(hexOf value: b) = (hexOf value: Float nan)]).
r add: ((hexOf value: (rt value: -0.0)) = '8000000000000000').
s := 'a', (String with: (Character value: 0)), 'b'.
r add: (((rt value: s) = s) and: [(rt value: s) size = 3]).
r add: ((rt value: 'h', (String with: (Character value: 233)), ' world') = ('h', (String with: (Character value: 233)), ' world')).
r add: (((rt value: #[0 1 255]) = #[0 1 255]) and: [(rt value: #[]) = #[]]).
o := OrderedCollection new. o add: 1; add: 'two'; add: #[3].
r add: ((rt value: o) = (Array with: 1 with: 'two' with: #[3])).
d := Dictionary new. d at: 'a' put: 1; at: 'b' put: #(2 3); at: #[9] put: nil.
r add: ((rt value: d) = d).
m := MessagePackMap withAll: {'z' -> 1. 'a' -> 2. 'm' -> 3}.
b := MessagePack encode: m.
r add: (b = #[16r83 16rA1 16r7A 1 16rA1 16r61 2 16rA1 16r6D 3]).
r add: (((MessagePack decodeOrdered: b) associations collect: [:a | a key]) = #('z' 'a' 'm')).
r add: ((rt value: (MessagePackExt type: 5 data: #[1 2 3])) = (MessagePackExt type: 5 data: #[1 2 3])).
shared := Array with: 1 with: 2.
r add: ((rt value: (Array with: shared with: shared)) = (Array with: shared with: shared)).
r add: ((rt value: #foo) = 'foo' and: [(rt value: #foo) class == String]).
r add: ((rt value: #aSymbolLongerThanSix) = 'aSymbolLongerThanSix').
b := MessagePack decodeFirst: #[1 2 3].
r add: (b key = 1 and: [b value = 1]).
r add: (((MessagePack decodeFirst: (MessagePack encode: #(1 2)), #[9 9]) value) = 3).
fails := (1 to: r size) select: [:i | (r at: i) ~~ true].
'failed: ', fails asArray printString
