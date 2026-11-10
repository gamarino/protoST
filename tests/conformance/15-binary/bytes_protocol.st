"EXPECT: ByteArray true false 6 #(2 4) 0102ff #[1 2 255] true #[1 9 9 4] #[1 2 3 4] #[] #[0 0] true true true"
"The rest of the ByteArray protocol: class, isEmpty, do:, collect: (an Array), inject:into:, hex and fromHex:, copyReplaceFrom:to:with: (a new value; the receiver is unchanged), new:, a #[...] nested in a literal Array, and the errors of withAll:, fromHex: and at:."
b := #[1 2 3 4].
sum := 0.
b do: [:x | sum := sum + x].
r := b copyReplaceFrom: 2 to: 3 with: #[9 9].
lit := #(#[7] 1).
e1 := [ByteArray withAll: #(1 256). false] on: Error do: [:e | true].
e2 := [ByteArray fromHex: 'zz'. false] on: Error do: [:e | true].
e3 := [b at: 5. false] on: Error do: [:e | true].
b class printString , ' ' , #[] isEmpty printString , ' ' , b isEmpty printString , ' ' ,
  (#[1 2 3] inject: 0 into: [:acc :x | acc + x]) printString , ' ' ,
  (#[1 2] collect: [:x | x * 2]) printString , ' ' ,
  #[1 2 255] hex , ' ' , (ByteArray fromHex: '0102FF') printString , ' ' ,
  (sum = 10) printString , ' ' , r printString , ' ' , b printString , ' ' ,
  (ByteArray new: 0) printString , ' ' , (ByteArray new: 2) printString , ' ' ,
  ((lit at: 1) = #[7]) printString , ' ' , (e1 and: [e2]) printString , ' ' , e3 printString.
