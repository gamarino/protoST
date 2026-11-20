"EXPECT: 0 failed of 288 checked"
"Float fsumOfPowSquaredDifferences:with: against CPython's math.fsum((x - y) ** 2 ...) (_vectors_fsum_pow.st, made by gen_fsum_vectors.py on glibc). Every answer is compared by its 8 IEEE bytes; an error case compares the Error's text; a NaN made by an invalid operation (inf - inf) matches any NaN. Each case runs on Arrays, on OrderedCollections and on a Smalltalk sequence that only answers size and at:, and once more against protoST's own Float fsum: ((1 to: n) collect: [:i | ((a at: i) - (b at: i)) raisedTo: 2]), which must answer the same bytes on every platform. A libmDependent case (some square where glibc's pow and d * d differ) pins glibc's bytes on Linux only: elsewhere the C library's pow may round those squares otherwise, and the case is held to the raisedTo: answer instead. Skipped on Windows: MSVC's pow(x, 2.0) is not correctly rounded on some exact half-way squares where glibc's is (CI measured 3 such cases, 9 checks), so glibc's bytes cannot be pinned there. CPython on Windows squares with that same pow, so protoST still agrees with it; float-fsum-pow-matches-raisedto.st holds these cases to raisedTo: 2 on every platform, Windows included."
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

FsumCheck class >> does: got match: expected
  "expected 'nan' is a NaN no input carried (an invalid operation made it):
   IEEE 754 leaves its sign and payload to the platform, so any NaN matches.
   Anything else must match the 8 bytes (or the error text) exactly."
  expected = 'nan' ifFalse: [^ got = expected].
  got size = 16 ifFalse: [^ false].
  ^ [((ByteArray fromHex: got) float64At: 1 bigEndian: true) isNaN] on: Error do: [:e | false].

FsumCheck class >> shapes: anArray
  ^ { anArray. (OrderedCollection withAll: anArray). (FsumSeq on: anArray) }.

FsumCheck class >> raisedToAnswer: xs with: ys
  "The same sum written with raisedTo: 2, as protoBaseST's kernel writes it."
  ^ self answer: [Float fsum: ((1 to: xs size) collect: [:i | ((xs at: i) - (ys at: i)) raisedTo: 2])].

| lib linux failed checked names got |
Smalltalk platform = 'windows'
  ifTrue: ['SKIP: the vectors pin glibc CPython''s pow; MSVC''s pow(x, 2.0) rounds some half-way squares otherwise (CPython on Windows uses that same pow; float-fsum-pow-matches-raisedto.st holds these cases to raisedTo: 2 here)']
  ifFalse: [
  lib := Import from: '_vectors_fsum_pow'.
  linux := Smalltalk platform = 'linux'.
  failed := 0.
  checked := 0.
  names := WriteStream on: String new.
  got := nil.
  lib VectorsFsumPow sqpows do: [:e |
    | xs ys as bs reference expected |
    xs := FsumCheck floats: (e at: 2).
    ys := FsumCheck floats: (e at: 3).
    as := FsumCheck shapes: xs.
    bs := FsumCheck shapes: ys.
    reference := FsumCheck raisedToAnswer: xs with: ys.
    expected := ((e at: 5) and: [linux not]) ifTrue: [reference] ifFalse: [e at: 4].
    1 to: 3 do: [:k |
      checked := checked + 1.
      got := FsumCheck answer: [Float fsumOfPowSquaredDifferences: (as at: k) with: (bs at: k)].
      (FsumCheck does: got match: expected)
        ifFalse: [failed := failed + 1. names nextPutAll: ' [' , (e at: 1) , ' #' , k printString , ' got ' , got , ']']].
    checked := checked + 1.
    got := FsumCheck answer: [Float fsumOfPowSquaredDifferences: xs with: ys].
    got = reference
      ifFalse: [failed := failed + 1. names nextPutAll: ' [' , (e at: 1) , ' raisedTo: answered ' , reference , ' got ' , got , ']']].
  failed printString, ' failed of ', checked printString, ' checked', names contents].
