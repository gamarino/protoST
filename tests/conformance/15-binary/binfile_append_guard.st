"EXPECT: 64 actors: every append succeeded or got the named Error, 0 other, offsets unique and tiled, 0 bad records"
"BinaryFile: the one-appender contract is enforced in process. Many actors append to one BinaryFile at once; each append either succeeds or signals exactly the 'another append is in progress' Error. The successful offsets are unique, offsets plus sizes tile the file with no gap or overlap, and every record reads back with a valid CRC32 (the file is never corrupted). How many appends collide depends on scheduling, so only the invariants are asserted."
Object subclass: #Appender.
Appender >> run: anId count: aCount file: aFile
  | out w filler body |
  w := WriteStream on: ByteArray new.
  4096 timesRepeat: [w nextPut: anId \\ 251].
  filler := w contents.
  out := OrderedCollection new.
  1 to: aCount do: [:i |
    w := WriteStream on: ByteArray new.
    w nextPutUnsigned: anId width: 4 bigEndian: true.
    w nextPutUnsigned: i width: 4 bigEndian: true.
    w nextPutAll: filler.
    body := w contents.
    w := WriteStream on: ByteArray new.
    w nextPutUnsigned: body size width: 4 bigEndian: false.
    w nextPutUnsigned: body crc32 width: 4 bigEndian: false.
    w nextPutAll: body.
    body := w contents.
    out add: ([(aFile append: body) -> body size] on: Error do: [:e | e messageText])].
  ^ out asArray
dir := FileSystem temp / ('protost-binfile-guard-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f actors results oks other named sorted expect bad recs rec len crc payload |
 f := BinaryFile open: (dir / 'g.bin') pathString options: #(#read #write #create #exclusive).
 actors := (1 to: 64) collect: [:id | Appender new asActor run: id count: 50 file: f].
 results := OrderedCollection new.
 actors do: [:a | results addAll: a wait].
 oks := results select: [:x | x isString not].
 named := results select: [:x | x isString and: [x includesSubstring: 'another append is in progress']].
 other := results size - oks size - named size.
 sorted := (oks asSortedCollection: [:a :b | a key <= b key]) asArray.
 expect := 0.
 bad := 0.
 sorted do: [:s | s key = expect ifFalse: [bad := bad + 1]. expect := s key + s value].
 f size = expect ifFalse: [bad := bad + 1].
 recs := 0.
 sorted do: [:s |
   rec := f readAt: s key size: s value.
   len := rec unsignedLongAt: 1 bigEndian: false.
   crc := rec unsignedLongAt: 5 bigEndian: false.
   payload := rec copyFrom: 9 to: rec size.
   (len = payload size and: [crc = payload crc32]) ifFalse: [bad := bad + 1]].
 f close.
 (oks isEmpty not and: [results size = 3200]) ifFalse: [bad := bad + 1].
 '64 actors: every append succeeded or got the named Error, ', other printString, ' other, offsets unique and tiled, ', bad printString, ' bad records']
  ensure: [dir deleteAll].
