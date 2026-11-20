"EXPECT: 22 decisive cases: pow answers, d * d does not"
"The pow vectors discriminate: in each decisive case of _vectors_fsum_pow.st, glibc's pow(d, 2.0) and d * d differ for some term and the two sums differ. Float fsumOfPowSquaredDifferences:with: must answer CPython's bytes, and Float fsumOfSquaredDifferences:with: (which multiplies) must not, so an implementation whose pow was folded into d * d fails here. glibc only: another C library's pow may round those squares otherwise."
Object subclass: #FsumCheck instanceVariableNames: ''.

FsumCheck class >> hexOf: aFloat
  | s |
  s := WriteStream on: ByteArray new.
  s nextPutFloat64: aFloat bigEndian: true.
  ^ s contents hex.

FsumCheck class >> floats: hexes
  ^ hexes collect: [:h | (ByteArray fromHex: h) float64At: 1 bigEndian: true].

FsumCheck class >> answer: aBlock
  ^ [self hexOf: aBlock value] on: Error do: [:e | e messageText].

| lib decisive failures |
lib := Import from: '_vectors_fsum_pow'.
decisive := 0.
failures := WriteStream on: String new.
Smalltalk platform = 'linux'
  ifFalse: ['SKIP: the pow vectors pin glibc''s pow; this platform''s C library may round the squares otherwise']
  ifTrue: [
    lib VectorsFsumPow sqpows do: [:e |
      | xs ys |
      (e at: 6) ifTrue: [
        decisive := decisive + 1.
        xs := FsumCheck floats: (e at: 2).
        ys := FsumCheck floats: (e at: 3).
        (FsumCheck answer: [Float fsumOfPowSquaredDifferences: xs with: ys]) = (e at: 4)
          ifFalse: [failures nextPutAll: ' [pow form wrong: ' , (e at: 1) , ']'].
        (FsumCheck answer: [Float fsumOfSquaredDifferences: xs with: ys]) = (e at: 4)
          ifTrue: [failures nextPutAll: ' [d * d agrees: ' , (e at: 1) , ']']]].
    decisive printString , ' decisive cases: pow answers, d * d does not' , failures contents].
