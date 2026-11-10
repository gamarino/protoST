"EXPECT: #[1 2 3 4 5] true true"
"<< on a byte stream: an Integer is a byte, a ByteArray or Array is appended, anything else is refused."
s := WriteStream on: ByteArray new.
s << 1 << #[2 3] << #(4 5).
bad := [:v | [(WriteStream on: ByteArray new) << v. false] on: Error do: [:e | true]].
s contents printString, ' ', (bad value: 'text') printString, ' ', ((bad value: nil) and: [bad value: $a]) printString.
