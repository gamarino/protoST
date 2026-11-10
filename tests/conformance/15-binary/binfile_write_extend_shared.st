"EXPECT: 16 actors x 200 extending writes at disjoint slots, no appender: 0 errors, 0 bad reads"
"BinaryFile: writeAt:bytes: calls that extend the file hold the end guard SHARED. Sixteen actors write 16-byte records at disjoint slots at ever-growing offsets, so most calls extend the file, and there is no appender. None may be refused (the guard is shared among extending writes), and every record reads back exactly."
Object subclass: #Writer.
Writer >> run: anId count: aCount file: aFile tickets: aTicketAtom
  | out off rec n |
  out := OrderedCollection new.
  1 to: aCount do: [:i |
    "The next free slot, from a shared counter: slots are disjoint and keep growing, so most writes extend the file."
    [n := aTicketAtom value. aTicketAtom value: n + 1 ifCurrent: n] whileFalse.
    off := 1000000 + (n * 16).
    rec := (WriteStream on: ByteArray new) nextPutUnsigned: anId width: 8 bigEndian: true; nextPutUnsigned: i width: 8 bigEndian: true; contents.
    out add: ([aFile writeAt: off bytes: rec. (off -> rec)] on: Error do: [:e | e messageText])].
  ^ out asArray
dir := FileSystem temp / ('protost-binfile-wshared-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f tickets writers results errors bad |
 f := BinaryFile open: (dir / 'w.bin') pathString options: #(#read #write #create #exclusive).
 tickets := Atom on: 0.
 writers := (1 to: 16) collect: [:id | Writer new asActor run: id count: 200 file: f tickets: tickets].
 results := OrderedCollection new.
 writers do: [:w | results addAll: w wait].
 errors := results select: [:x | x isString].
 bad := (results reject: [:x | x isString or: [(f readAt: x key size: 16) = x value]]) size.
 f close.
 '16 actors x 200 extending writes at disjoint slots, no appender: ', errors size printString, ' errors, ', bad printString, ' bad reads']
  ensure: [dir deleteAll].
