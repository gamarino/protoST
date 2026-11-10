"EXPECT: 16 writers + 1 appender: every call succeeded or got the named Error, 0 other, 0 bad bytes, both kinds refused at least once: true"
"BinaryFile: an extending writeAt:bytes: (shared) and append: (exclusive) exclude each other. Sixteen actors write 16-byte records at disjoint slots at ever-growing offsets while one actor appends 16-byte records. Each call succeeds or signals the named Error. A write and an append can land on the same bytes, because append: writes at the end and the writers' slots lie ahead of it; that overwrite is the caller's race and not what is under test. So the bytes of a successful call are verified only when no successful call of the other kind touched them: a slot overlapped by an appended record, and the record overlapping it, are skipped. Which calls collide depends on scheduling, so only invariants are asserted (plus that each kind was refused at least once, which with 3,600 overlapping calls on a pool of two or more workers always happens, and is how the exclusion shows): no unnamed error, appended offsets unique, every non-overlapped write and append reads back exactly."
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
Object subclass: #Tail.
Tail >> run: aCount file: aFile
  | out rec |
  out := OrderedCollection new.
  1 to: aCount do: [:i |
    rec := (WriteStream on: ByteArray new) nextPutUnsigned: 999 width: 8 bigEndian: true; nextPutUnsigned: i width: 8 bigEndian: true; contents.
    out add: ([(aFile append: rec) -> rec] on: Error do: [:e | e messageText])].
  ^ out asArray
dir := FileSystem temp / ('protost-binfile-wguard-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f tickets writers tail wres ares wOk aOk named other bad overlappedSlots slots offs |
 f := BinaryFile open: (dir / 'w.bin') pathString options: #(#read #write #create #exclusive).
 tickets := Atom on: 0.
 writers := (1 to: 16) collect: [:id | Writer new asActor run: id count: 200 file: f tickets: tickets].
 tail := Tail new asActor run: 400 file: f.
 wres := OrderedCollection new.
 writers do: [:w | wres addAll: w wait].
 ares := tail wait.
 wOk := wres reject: [:x | x isString].
 aOk := ares reject: [:x | x isString].
 named := (wres , ares) select: [:x | x isString and: [x includesSubstring: 'moves the end of the file is in progress' ]].
 named := named , ((wres , ares) select: [:x | x isString and: [x includesSubstring: 'an append: or truncateTo: is in progress']]).
 other := (wres size + ares size) - wOk size - aOk size - named size.
 bad := 0.
 "Slot numbers of the successful writes, and of those an appended record overlaps."
 slots := Set new.
 wOk do: [:x | slots add: (x key - 1000000) // 16].
 overlappedSlots := Set new.
 offs := Set new.
 aOk do: [:x | | first last hit |
   (offs includes: x key) ifTrue: [bad := bad + 1].
   offs add: x key.
   first := (x key - 1000000 - 15) // 16.
   last := (x key + 16 - 1 - 1000000) // 16.
   hit := false.
   first to: last do: [:k | (slots includes: k) ifTrue: [hit := true. overlappedSlots add: k]].
   hit ifFalse: [(f readAt: x key size: 16) = x value ifFalse: [bad := bad + 1]]].
 wOk do: [:x | (overlappedSlots includes: (x key - 1000000) // 16)
   ifFalse: [(f readAt: x key size: 16) = x value ifFalse: [bad := bad + 1]]].
 f close.
 '16 writers + 1 appender: every call succeeded or got the named Error, ', other printString, ' other, ', bad printString, ' bad bytes, both kinds refused at least once: ', ((named size > 0) and: [(named select: [:x | x includesSubstring: 'append: another']) size > 0 and: [(named select: [:x | x includesSubstring: 'writeAt:bytes: an append']) size > 0]]) printString]
  ensure: [dir deleteAll].
