"EXPECT: #(true true true true true true nil)"
"protoIO 0.7.1: a socket's timeout: bounds a whole write, however large. A
 peer that never reads gets 8 MiB pieces until one fails (Windows buffers
 several): the write that fails signals ConnectionTimedOut, the error a read
 that times out signals, within the timeout plus a margin, and its
 bytesWritten says exactly how many leading bytes went out. Checked: the
 peer, once it reads, gets the bytes of the successful writes, the
 bytesWritten bytes, then the rest written after the error, all in order
 (byte i of the stream is i \\ 251). A read that times out answers
 bytesWritten nil. Before 0.7.1 the first 8 MiB write blocked in the kernel
 without limit. No duration is asserted except a 5 s bound on a 300 ms
 timeout."
Import from: 'net'.
Object subclass: #Hole.
Hole >> acceptOn: aServer
  ^ aServer accept.
Object subclass: #Writer.
Writer >> write: bytes on: aSocket
  aSocket nextPutBytes: bytes.
  ^ bytes size.

| piece base long server hole s peer sent err t0 slow rest got expected bad readErr w |
piece := 8388608.
base := ByteArray withAll: (0 to: 250).
long := base.
[long size < (piece + 251)] whileTrue: [long := long , long].
"The bytes of the stream from offset `from`, n of them (n <= piece)."
w := [:from :n | long copyFrom: (from \\ 251) + 1 to: (from \\ 251) + n].
server := ServerSocket listenOn: 0 host: '127.0.0.1'.
hole := Hole new asActor acceptOn: server.
s := Socket connectTo: '127.0.0.1' port: server port timeout: 2000.
peer := hole waitWithin: 5000.
s timeout: 300.
sent := 0. err := nil. slow := false.
[err isNil and: [sent < (64 * piece)]] whileTrue: [
  | bytes |
  bytes := w value: sent value: piece.
  t0 := Time millisecondClockValue.
  [s nextPutBytes: bytes. sent := sent + piece] on: ConnectionTimedOut do: [:e |
    err := e.
    rest := bytes copyFrom: e bytesWritten + 1 to: piece.
    sent := sent + e bytesWritten].
  (Time millisecondClockValue - t0) > 5300 ifTrue: [slow := true]].
"The rest goes out from an actor while the peer reads everything back."
s timeout: 30000.
peer timeout: 30000.
got := 0. expected := sent + rest size. bad := 0.
w := Writer new asActor write: rest on: s.
[got < expected] whileTrue: [
  | bytes |
  bytes := peer nextBytes: (expected - got min: 1048576).
  bytes = (long copyFrom: (got \\ 251) + 1 to: (got \\ 251) + bytes size) ifFalse: [bad := bad + 1].
  got := got + bytes size].
w wait.
peer timeout: 100.
readErr := [peer nextBytes: 1. 'read'] on: ConnectionTimedOut do: [:e | e bytesWritten].
s close. peer close. server close.
{ err notNil. err class == ConnectionTimedOut. slow not.
  err bytesWritten < piece. got = expected. bad = 0. readErr } printString
