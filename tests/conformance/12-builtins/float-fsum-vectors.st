"EXPECT: 0 failed of 711 checked"
"Float fsum:, fsumOfProducts:with: and fsumOfSquaredDifferences:with: against CPython's math.fsum (_vectors_fsum.st, made by gen_fsum_vectors.py). Every answer is compared by its 8 IEEE bytes, so -0.0, the infinities and NaN payloads count; an error case compares the Error's text. Each case runs three times: on Arrays, on OrderedCollections, and on a Smalltalk sequence that only answers size and at: (as protoBaseST's PBTuple does)."
Object subclass: #FsumSeq instanceVariableNames: 'items'.

FsumSeq class >> on: anArray
  ^ self new setItems: anArray.

FsumSeq >> setItems: anArray
  items := anArray.

FsumSeq >> size
  ^ items size.

FsumSeq >> at: anInteger
  ^ items at: anInteger.

Object subclass: #FsumCheck instanceVariableNames: ''.

FsumCheck class >> hexOf: aFloat
  | s |
  s := WriteStream on: ByteArray new.
  s nextPutFloat64: aFloat bigEndian: true.
  ^ s contents hex.

FsumCheck class >> floats: hexes
  ^ hexes collect: [:h | (ByteArray fromHex: h) float64At: 1 bigEndian: true].

FsumCheck class >> answer: aBlock
  "The hex bits of the Float aBlock answers, or the text of the Error it signals."
  ^ [self hexOf: aBlock value] on: Error do: [:e | e messageText].

FsumCheck class >> shapes: anArray
  ^ { anArray. (OrderedCollection withAll: anArray). (FsumSeq on: anArray) }.

lib := Import from: '_vectors_fsum'.
failed := 0.
checked := 0.
lib VectorsFsum sums do: [:e |
  (FsumCheck shapes: (FsumCheck floats: (e at: 2))) do: [:seq |
    checked := checked + 1.
    (FsumCheck answer: [Float fsum: seq]) = (e at: 3)
      ifFalse: [failed := failed + 1. ('sum: ' , (e at: 1)) displayNl]]].
lib VectorsFsum products do: [:e |
  | as bs |
  as := FsumCheck shapes: (FsumCheck floats: (e at: 2)).
  bs := FsumCheck shapes: (FsumCheck floats: (e at: 3)).
  1 to: 3 do: [:k |
    checked := checked + 1.
    (FsumCheck answer: [Float fsumOfProducts: (as at: k) with: (bs at: k)]) = (e at: 4)
      ifFalse: [failed := failed + 1. ('products: ' , (e at: 1)) displayNl]]].
lib VectorsFsum sqdiffs do: [:e |
  | as bs |
  as := FsumCheck shapes: (FsumCheck floats: (e at: 2)).
  bs := FsumCheck shapes: (FsumCheck floats: (e at: 3)).
  1 to: 3 do: [:k |
    checked := checked + 1.
    (FsumCheck answer: [Float fsumOfSquaredDifferences: (as at: k) with: (bs at: k)]) = (e at: 4)
      ifFalse: [failed := failed + 1. ('sqdiffs: ' , (e at: 1)) displayNl]]].
failed printString, ' failed of ', checked printString, ' checked'.
