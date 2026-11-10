"EXPECT: 12 5050 12"
"BinaryFile: lock waits in the calling worker only. Twelve actors, more than the default pool of at most 8 workers, all wait at once for a lock the main program holds: the pool grows, other actors still run, collections still happen, and each waiter gets the lock only after it is released."
Object subclass: #Locker.
Locker >> take: aFile started: startedAtom released: releasedAtom
  | seen |
  [| n | n := startedAtom value. startedAtom value: n + 1 ifCurrent: n] whileFalse.
  aFile lock.
  seen := releasedAtom value.
  aFile unlock.
  ^ seen
Object subclass: #Adder.
Adder >> sumTo: n
  ^ (1 to: n) inject: 0 into: [:acc :each | acc + each]
dir := FileSystem temp / ('protost-binfile-lock-actors-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| path holder files started released waiters deadline sum junk seen |
 path := (dir / 'owner.lock') pathString.
 holder := BinaryFile open: path options: #(#read #write #create).
 holder tryLock.
 "Opened here, so the waiters do no other blocking call that could grow the pool: only lock does."
 files := (1 to: 12) collect: [:i | BinaryFile open: path options: #(#read #write)].
 started := Atom on: 0.
 released := Atom on: false.
 waiters := files collect: [:f | Locker new asActor take: f started: started released: released].
 "Every waiter starts (it is in lock, or about to enter it). With a pool that
  did not grow, the last four would never start: after 10 s the test gives
  up, releases the lock so every actor can finish, and reports the count."
 deadline := Time millisecondClockValue + 10000.
 [started value < 12 and: [Time millisecondClockValue < deadline]] whileTrue.
 sum := started value = 12 ifTrue: [(Adder new asActor sumTo: 100) wait] ifFalse: [0].
 1 to: 20000 do: [:i | junk := Array new: 8].
 released value: true ifCurrent: false.
 holder unlock.
 seen := (waiters collect: [:each | each wait]) select: [:each | each].
 files do: [:f | f close].
 holder close.
 started value printString , ' ' , sum printString , ' ' , seen size printString]
  ensure: [dir deleteAll].
