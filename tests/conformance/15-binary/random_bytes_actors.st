"EXPECT: 200 true"
"ByteArray randomBytes: and Time utcMicroseconds from four actors at once:
 the draw runs outside the collector's quorum (unmanaged) and protoIO keeps
 no generator state, so concurrent calls need no lock (the ThreadSanitizer
 run covers this program: it uses asActor). Each actor answers 50 16-byte
 draws; all 200 are distinct (a repeat has probability below 2^-112), and
 every clock reading is after 2026-01-01."
Object subclass: #Drawer.
Drawer >> draws
  | out |
  out := OrderedCollection new.
  50 timesRepeat: [
    out add: (ByteArray randomBytes: 16).
    Time utcMicroseconds > 1767225600000000 ifFalse: [^ nil]].
  ^ out asArray.

| futures seen all ok |
futures := (1 to: 4) collect: [:i | Drawer new asActor draws].
seen := Set new.
ok := true.
futures do: [:f | | r |
  r := f wait.
  r isNil ifTrue: [ok := false] ifFalse: [r do: [:b | seen add: b]]].
seen size printString , ' ' , ok printString.
