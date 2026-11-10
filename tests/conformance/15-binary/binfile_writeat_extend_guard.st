"EXPECT: 16 actors: every extending write succeeded or got the named Error, 0 other, 0 bad reads"
"BinaryFile: a writeAt:bytes: that extends the file takes the same claim as append:. Sixteen actors write 16-byte records at ever-growing offsets (each write extends the file or lands inside what another extended) while one actor appends. Each call either succeeds or signals the 'another append is in progress' Error; every successful write reads back exactly. How many collide depends on scheduling and is not asserted."
Object subclass: #Writer.
Writer >> run: anId count: aCount file: aFile
  | out off rec |
  out := OrderedCollection new.
  1 to: aCount do: [:i |
    off := 1000000 + (((i - 1) * 16 + anId - 1) * 16).
    rec := (WriteStream on: ByteArray new) nextPutUnsigned: anId width: 8 bigEndian: true; nextPutUnsigned: i width: 8 bigEndian: true; contents.
    out add: ([aFile writeAt: off bytes: rec. (off -> rec)] on: Error do: [:e | e messageText])].
  ^ out asArray
Object subclass: #Tail.
Tail >> run: aCount file: aFile
  | out |
  out := OrderedCollection new.
  aCount timesRepeat: [out add: ([aFile append: #[1 2 3 4]. true] on: Error do: [:e | e messageText])].
  ^ out asArray
dir := FileSystem temp / ('protost-binfile-wguard-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f writers tail results oks named other bad |
 f := BinaryFile open: (dir / 'w.bin') pathString options: #(#read #write #create #exclusive).
 writers := (1 to: 16) collect: [:id | Writer new asActor run: id count: 200 file: f].
 tail := Tail new asActor run: 400 file: f.
 results := OrderedCollection new.
 writers do: [:w | results addAll: w wait].
 tail wait.
 oks := results select: [:x | x isString not].
 named := results select: [:x | x isString and: [x includesSubstring: 'another append is in progress']].
 other := results size - oks size - named size.
 bad := (oks reject: [:x | (f readAt: x key size: 16) = x value]) size.
 f close.
 '16 actors: every extending write succeeded or got the named Error, ', other printString, ' other, ', bad printString, ' bad reads']
  ensure: [dir deleteAll].
