"EXPECT: true false true true true"
"BinaryFile: the lock belongs to the open file. A second descriptor in the same process is excluded until unlock; lock takes a free lock; close releases it."
dir := FileSystem temp / ('protost-binfile-lock-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| path a b first second third afterLock afterClose |
 path := (dir / 'owner.lock') pathString.
 a := BinaryFile open: path options: #(#read #write #create).
 b := BinaryFile open: path options: #(#read #write #create).
 first := a tryLock.
 second := b tryLock.
 a unlock.
 third := b tryLock.
 b unlock.
 a lock.
 afterLock := b tryLock not.
 a close.
 afterClose := b tryLock.
 b close.
 first printString , ' ' , second printString , ' ' , third printString , ' ' , afterLock printString , ' ' ,
   afterClose printString]
  ensure: [dir deleteAll].
