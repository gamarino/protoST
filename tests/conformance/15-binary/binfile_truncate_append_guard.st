"EXPECT: appender + truncator: every call succeeded or got the named Error, 0 other, 0 torn records"
"BinaryFile: truncateTo: needs the file alone and append: the end of it. One actor appends CRC-framed records of about 2 MiB while another repeatedly truncates the file back to a 128-byte header. Each call succeeds or signals the named Error. Because the two never overlap in time, a truncate cuts between whole appends: the final file is the header followed by whole, valid frames up to its exact end The truncator runs for as long as the appender does and every append holds the guard through a multi-millisecond write, so on a multi-worker pool it is refused many times. The number of refused calls is deliberately not asserted: it depends on the worker count and timing (it is zero on a single worker), and the guard's effectiveness is shown by mutation runs recorded in the task report. (A truncate landing between an append's seek and write would leave a zero-filled hole that breaks the frame walk)."
Object subclass: #Tail.
Tail >> run: aCount file: aFile
  | out w payload body unit |
  "Records of about 2 MiB, so an append spends a long time inside its write and a
   truncate has every chance to land in the middle of it if nothing prevents that."
  w := WriteStream on: ByteArray new.
  65536 timesRepeat: [w nextPut: 9].
  unit := w contents.
  5 timesRepeat: [unit := unit , unit].
  out := OrderedCollection new.
  1 to: aCount do: [:i |
    w := WriteStream on: ByteArray new.
    w nextPutUnsigned: i width: 4 bigEndian: true.
    w nextPutAll: unit.
    payload := w contents.
    w := WriteStream on: ByteArray new.
    w nextPutUnsigned: payload size width: 4 bigEndian: false.
    w nextPutUnsigned: payload crc32 width: 4 bigEndian: false.
    w nextPutAll: payload.
    body := w contents.
    out add: ([aFile append: body. true] on: Error do: [:e | e messageText])].
  ^ out asArray
Object subclass: #Cutter.
Cutter >> run: aDoneAtom file: aFile
  | out n |
  out := OrderedCollection new.
  n := 0.
  [aDoneAtom value or: [(n := n + 1) > 200000]] whileFalse: [out add: ([aFile truncateTo: 128. true] on: Error do: [:e | e messageText])].
  ^ out asArray
dir := FileSystem temp / ('protost-binfile-tguard-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f done header tail cutter results named other pos torn size hdr len crc payload |
 f := BinaryFile open: (dir / 't.bin') pathString options: #(#read #write #create #exclusive).
 header := WriteStream on: ByteArray new.
 128 timesRepeat: [header nextPut: 7].
 f append: header contents.
 done := Atom on: false.
 tail := Tail new asActor run: 40 file: f.
 cutter := Cutter new asActor run: done file: f.
 results := OrderedCollection new.
 results addAll: tail wait.
 done value: true ifCurrent: false.
 results addAll: cutter wait.
 named := results select: [:x | x isString and: [(x includesSubstring: 'BinaryFile>>append: another operation that moves the end of the file is in progress on this BinaryFile') or: [x includesSubstring: 'BinaryFile>>truncateTo: another append:, writeAt:bytes: or truncateTo: is in progress on this BinaryFile']]].
 other := results size - (results select: [:x | x == true]) size - named size.
 torn := 0.
 size := f size.
 size < 128 ifTrue: [torn := torn + 1].
 pos := 128.
 [pos < size and: [torn = 0]] whileTrue: [
   hdr := f readAt: pos size: 8.
   hdr size = 8 ifFalse: [torn := torn + 1] ifTrue: [
     len := hdr unsignedLongAt: 1 bigEndian: false.
     crc := hdr unsignedLongAt: 5 bigEndian: false.
     payload := f readAt: pos + 8 size: len.
     ((len > 0) and: [payload size = len and: [crc = payload crc32]]) ifFalse: [torn := torn + 1].
     pos := pos + 8 + len]].
 pos = size ifFalse: [torn := torn + 1].
 f close.
 'appender + truncator: every call succeeded or got the named Error, ', other printString, ' other, ', torn printString, ' torn records']
  ensure: [dir deleteAll].
