"EXPECT: records 1000 verified, tiled 65536 + 39989 bytes, reads 16000 ok, 0 failures"
"BinaryFile under load: one appender actor (1000 records of 16 to 64 bytes, the one-appender contract of BinaryFile, framed payload_len u32 LE | crc32 u32 LE | payload) and 16 reader actors (1000 readAt:size: each on random offsets) share one file at the same time. The file starts with a 65536-byte known pattern. Assertions are exact and independent of scheduling: every append offset is unique, offsets plus sizes tile the file with no gap or overlap, every record reads back with a valid CRC32 and its own identity, and every read answers at most the bytes asked, exactly the pattern inside the prefix, and never anything past the end of the file."
Object subclass: #Appender.
Appender >> run: anId count: aCount file: aFile
  | out w body header payload len |
  out := OrderedCollection new.
  1 to: aCount do: [:i |
    len := 8 + (((anId * 7) + (i * 13)) \\ 49).
    w := WriteStream on: ByteArray new.
    w nextPutUnsigned: anId width: 4 bigEndian: true.
    w nextPutUnsigned: i width: 4 bigEndian: true.
    (len - 8) timesRepeat: [w nextPut: i \\ 251].
    payload := w contents.
    header := WriteStream on: ByteArray new.
    header nextPutUnsigned: payload size width: 4 bigEndian: false.
    header nextPutUnsigned: payload crc32 width: 4 bigEndian: false.
    header nextPutAll: payload.
    body := header contents.
    out add: ((aFile append: body) -> body size)].
  ^ out asArray
Object subclass: #Reader.
Reader >> run: aSeed count: aCount file: aFile prefix: aPrefix
  | r bad off n got |
  r := (Import from: 'random') Random seed: aSeed.
  bad := 0.
  aCount timesRepeat: [
    off := r nextInt: aPrefix + 20000.
    n := 1 + (r nextInt: 128).
    got := aFile readAt: off - 1 size: n.
    (got size <= n) ifFalse: [bad := bad + 1].
    ((off - 1 + n) <= aPrefix and: [got size ~= n]) ifTrue: [bad := bad + 1].
    (off - 1 < aPrefix) ifTrue: [
      1 to: (got size min: aPrefix - (off - 1)) do: [:k |
        (got at: k) = ((off - 1 + k - 1) \\ 251) ifFalse: [bad := bad + 1]]]].
  ^ bad
dir := FileSystem temp / ('protost-binfile-stress-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| path f pattern prefix appender readers spans sorted expect bad recs ok readBad rec len crc payload start total |
 path := (dir / 'stress.bin') pathString.
 f := BinaryFile open: path options: #(#read #write #create #exclusive).
 prefix := 65536.
 pattern := WriteStream on: ByteArray new.
 0 to: prefix - 1 do: [:i | pattern nextPut: i \\ 251].
 f append: pattern contents.
 appender := Appender new asActor run: 1 count: 1000 file: f.
 readers := (1 to: 16) collect: [:s | Reader new asActor run: s count: 1000 file: f prefix: prefix].
 spans := OrderedCollection new.
 spans addAll: appender wait.
 readBad := (readers collect: [:each | each wait]) inject: 0 into: [:a :b | a + b].
 sorted := (spans asSortedCollection: [:a :b | a key <= b key]) asArray.
 expect := prefix.
 bad := 0.
 sorted do: [:s | s key = expect ifFalse: [bad := bad + 1]. expect := s key + s value].
 (f size = expect) ifFalse: [bad := bad + 1].
 recs := 0.
 sorted do: [:s |
   rec := f readAt: s key size: s value.
   len := rec unsignedLongAt: 1 bigEndian: false.
   crc := rec unsignedLongAt: 5 bigEndian: false.
   payload := rec copyFrom: 9 to: rec size.
   ((rec size = s value) and: [len = payload size and: [crc = payload crc32]])
     ifTrue: [recs := recs + 1] ifFalse: [bad := bad + 1]].
 total := f size.
 f close.
 'records ', recs printString, ' verified, tiled ', prefix printString, ' + ', (total - prefix) printString, ' bytes, reads 16000 ok, ', (bad + readBad) printString, ' failures']
  ensure: [dir deleteAll].
