"Kernel: BinaryFile, a positionless binary file with durable writes and an
 inter-process lock.

 A BinaryFile is a handle to an OS resource, not a value: it is the one place
 where changing the world outside the program is explicit. It has no hidden
 position. Every read and write names its offset, so one BinaryFile can be
 shared by several actors with no cursor to race on.

 Contract: ONE APPENDER AT A TIME per BinaryFile; any number of readers.
   - readAt:size: may run in any number of actors at once.
   - append: answers the offset where its bytes start. That answer is only
     well defined while a single actor appends: neither POSIX nor Windows
     says atomically where an append landed. Concurrent appenders on one
     BinaryFile, or another process appending to the same file, are not
     supported.
   - What is guarded, in this process, on one BinaryFile object: the end
     guard (protoIO's file::EndGuard). The call that finds a conflicting
     call in progress signals an Error naming its selector and does nothing:
       append:          refused while an append:, a truncateTo: or a
                        writeAt:bytes: that extends the file is in progress;
       writeAt:bytes:   refused while a truncateTo: is in progress, and, when
                        it extends the file, while an append: is;
       truncateTo:      refused while any append:, writeAt:bytes: or
                        truncateTo: is in progress (it needs the file alone).
     Writes run together with each other, and with appends while they stay
     inside the file. So, on one BinaryFile: the bytes of an append: land at
     the offset it answers, nothing else moves the end of the file while it
     writes them, and no write lands past the end of a file truncateTo: cut
     meanwhile (a write judged inside the file stays inside). An appended
     record is overwritten only by a writeAt:bytes: whose range its caller
     chose to overlap it; two writes over the same bytes are never ordered,
     as in any file API.
   - What cannot be detected: appenders or writers in other processes, or
     through another BinaryFile object opened on the same file (take lock
     around them). (A protoBaseST segment has a single writer actor.)
   - append: needs a regular file: on a pipe, a FIFO, a socket or a device it
     signals FileSystemError and writes nothing.
   - writeAt:bytes: overwrites bytes at an offset, extending the file as
     needed. A file opened with #append refuses it on every platform.

 Durability, exactly as protoIO defines it (protoio/file.h):
   - sync: the file's data and metadata are durable (fsync; FlushFileBuffers
     on Windows). As CPython's os.fsync, on macOS it is plain fsync, not
     F_FULLFSYNC, so the drive's own cache may still hold the data there.
   - dataSync: the file's data is durable, with the metadata needed to read
     it back (fdatasync on Linux, fsync on macOS, FlushFileBuffers on
     Windows). It is never a no-op.
   - FileSystem syncDirectory: makes a directory's entries (created, renamed
     or removed names) durable. On Windows, where a directory cannot be
     synced, it does nothing, as ProtoBase's fsync_dir.
   - FileSystem replace: from with: to renames, replacing an existing file
     atomically (CPython's os.replace).
   The usual durable replacement: write 'root.tmp', sync it, replace it over
   'root', then syncDirectory: the directory.

 The lock is the one ProtoBase's OwnerLock takes (flock on POSIX, LockFileEx
 on byte 0 on Windows), so a protoST process and a Python ProtoBase process
 exclude each other. It is exclusive and advisory, and it belongs to the open
 file, not the process: a second BinaryFile opened on the same path, even in
 the same program, does not share it. Take it once per BinaryFile. It is
 released by unlock or by close. On Windows the locked byte cannot be read or
 written through another descriptor, so lock a dedicated file.
   - tryLock answers true when it took the lock, false when another holder
     has it.
   - lock waits while another holder has it. It blocks the calling thread
     only: an actor that waits in lock handles no other message meanwhile,
     but the runtime starts another worker so other actors keep running, and
     the waiting thread never holds up a collection.

 Lifetime: close releases the descriptor and the lock. A second close does
 nothing; any other use after close signals Error with the text 'BinaryFile
 is closed'. close itself never waits: the handle is closed at once for
 every later use. But while another actor is still inside an operation on
 the same file (waiting in lock for another holder, or in a long sync), that
 operation finishes on the same descriptor, and the close takes effect, the
 descriptor and its lock being released, only when it returns. A lock or
 tryLock that gets the lock after such a close never answers success: it
 releases the lock at once and signals Error 'BinaryFile is closed (the lock
 was released)', so no actor believes it holds a lock that the close has
 dropped. (A lock waiting for another holder is not cut short by the close:
 it waits until that holder releases.) Nothing is closed when the collector
 frees an unreachable BinaryFile: an unclosed file stays open, and keeps its
 lock, until the process exits.

 Bytes are ByteArrays: append: and writeAt:bytes: take one, readAt:size:
 answers one. Errors: FileDoesNotExist, FileAlreadyExists and FileSystemError
 (lib/kernel/io.st) for the file system; Error for a wrong argument."

Object subclass: #BinaryFile instanceVariableNames: 'handle path'.

BinaryFile class >> options
  "The option symbols open:options: accepts, in the order of the primitive's flag bits."
  ^ #(#read #write #create #exclusive #truncate #append).

BinaryFile class >> open: aPath options: anArrayOfSymbols
  "Opens the file at aPath (a String or a FileReference). The options:
     #read       reading (readAt:size:);
     #write      writing (writeAt:bytes:, append:, truncateTo:);
     #create     create the file when it is missing;
     #exclusive  with #create: FileAlreadyExists when it is already there;
     #truncate   with #write: empty the file;
     #append     every write goes to the end (implies #write; refuses
                 writeAt:bytes:).
   At least one of #read, #write or #append is needed. An unknown option
   signals Error before anything is opened."
  | known flags p |
  anArrayOfSymbols isArray ifFalse: [^ Error signal: 'BinaryFile class>>open:options: an Array of Symbols was expected'].
  known := self options.
  flags := 0.
  anArrayOfSymbols do: [:each | | i |
    i := known indexOf: each.
    i = 0 ifTrue: [^ Error signal: 'BinaryFile: unknown option '
      , (each isString ifTrue: ['#' , each] ifFalse: [each printString])].
    flags := flags bitOr: (1 bitShift: i - 1)].
  p := aPath asFileReference pathString.
  ^ self basicNew setHandle: (self __binFileOpen: p flags: flags) path: p.

BinaryFile >> setHandle: anInteger path: aString
  handle := anInteger.
  path := aString.
  ^ self.

BinaryFile >> path
  "The path the file was opened with."
  ^ path.

BinaryFile >> append: aByteArray
  "Writes aByteArray at the end of the file and answers the offset where it
   starts. One appender at a time (see the class comment)."
  ^ self __binFileAppend: handle data: aByteArray.

BinaryFile >> readAt: anOffset size: aCount
  "Up to aCount bytes from anOffset, as a ByteArray: fewer only at the end of
   the file, and none from the end on."
  ^ self __binFileReadAt: handle offset: anOffset size: aCount.

BinaryFile >> writeAt: anOffset bytes: aByteArray
  "Writes all of aByteArray at anOffset, extending the file as needed."
  self __binFileWriteAt: handle offset: anOffset data: aByteArray.
  ^ self.

BinaryFile >> size
  "The size of the file, in bytes."
  ^ self __binFileSize: handle.

BinaryFile >> linkCount
  "The number of hard links (names) of the open file. It is read from the
   open descriptor, not the path, so it describes the file this BinaryFile
   opened even if the path was renamed or replaced since: a store whose lock
   file is named after the path of its data file checks that the file it
   opened has exactly one name (linkCount = 1) before reading or repairing
   it, as ProtoBase's log does. On POSIX a file whose last name was removed
   while open answers 0. The count is a snapshot: another process may add or
   remove a name at once."
  ^ self __binFileLinkCount: handle.

BinaryFile >> truncateTo: aSize
  "Sets the size of the file: cuts it, or extends it with zero bytes."
  self __binFileTruncate: handle to: aSize.
  ^ self.

BinaryFile >> sync
  "Makes the file's data and metadata durable (see the class comment)."
  self __binFileSync: handle.
  ^ self.

BinaryFile >> dataSync
  "Makes the file's data durable, with the metadata needed to read it back
   (see the class comment)."
  self __binFileDataSync: handle.
  ^ self.

BinaryFile >> tryLock
  "Takes the lock without waiting: true when taken, false when another
   holder has it."
  ^ self __binFileTryLock: handle.

BinaryFile >> lock
  "Takes the lock, waiting while another holder has it. Only the calling
   thread waits (see the class comment)."
  self __binFileLock: handle.
  ^ self.

BinaryFile >> unlock
  self __binFileUnlock: handle.
  ^ self.

BinaryFile >> close
  "Releases the descriptor and the lock. Closing twice does nothing."
  self __binFileClose: handle.
  ^ self.

BinaryFile >> isClosed
  ^ (self __binFileIsOpen: handle) not.

BinaryFile >> printOn: aStream
  aStream nextPutAll: 'a BinaryFile on ' , path.
  self isClosed ifTrue: [aStream nextPutAll: ' (closed)'].
