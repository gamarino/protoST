"EXPECT: true true true"
"BinaryFile: a close while another actor waits in lock never lets that lock answer success. The waiter keeps waiting until the holder releases (flock cannot be interrupted), then signals Error and leaves the lock free.
 Answers: the waiter got Error 'BinaryFile is closed'; nobody holds the lock afterwards; the close came while the waiter was inside flock, so the message says the lock was released. The handshake that proves the waiter is inside flock reads Linux's /proc/locks; elsewhere the test reports SKIP (see README.md) instead of passing without exercising the fix."
Object subclass: #Waiter.
Waiter >> take: aFile started: anAtom
  anAtom value: true ifCurrent: false.
  ^ [aFile lock. 'lock answered success'] on: Error do: [:e | e messageText]
dir := FileSystem temp / ('protost-binfile-lock-close-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| path holder waited started waiter linux blocked deadline answer free probe |
 path := (dir / 'owner.lock') pathString.
 holder := BinaryFile open: path options: #(#read #write #create).
 waited := BinaryFile open: path options: #(#read #write).
 holder lock.
 started := Atom on: false.
 waiter := Waiter new asActor take: waited started: started.
 [started value] whileFalse.
 "The handshake, on Linux: the kernel lists a blocked flock request of this
  process in /proc/locks (a line with '->') once the waiter is inside flock.
  Bounded at 10 s; blocked stays false if it never appears, failing the test."
 linux := Smalltalk platform = 'linux'.
 blocked := false.
 linux ifTrue: [
   deadline := Time millisecondClockValue + 10000.
   [blocked := ('/proc/locks' asFileReference lines) anySatisfy: [:line |
       (line includesSubstring: '-> FLOCK') and: [line includesSubstring: ' ' , Smalltalk pid printString , ' ']].
    blocked not and: [Time millisecondClockValue < deadline]] whileTrue].
 waited close.
 holder unlock.
 answer := waiter wait.
 "Nobody holds the lock: the waiter released what flock gave it."
 probe := BinaryFile open: path options: #(#read #write).
 free := probe tryLock.
 probe close.
 holder close.
 linux
   ifTrue: [(answer includesSubstring: 'BinaryFile is closed') printString , ' ' , free printString , ' ' ,
     (blocked and: [answer = 'BinaryFile>>lock: BinaryFile is closed (the lock was released)']) printString]
   ifFalse: ['SKIP: no /proc/locks on ' , Smalltalk platform , ', so a close while lock waits is NOT verified']]
  ensure: [dir deleteAll].
