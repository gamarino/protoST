"Kernel: input and output.

 Standard streams (Stdio), files and directories (FileReference, FileSystem,
 File), the program's arguments and environment (Smalltalk), and other
 programs (OSProcess). Sockets and HTTP are loadable modules: `Import from:
 'net'` and `Import from: 'http'`.

 Every blocking operation leaves the collector's quorum while it waits, so a
 program or an actor blocked on input never holds up a collection. Errors are
 classed: FileDoesNotExist, FileAlreadyExists, FileSystemError, NetworkError
 and its subclasses, OSProcessError, LineTooLong. Text is UTF-8; binary data is an Array
 of integers from 0 to 255. See docs/superpowers/specs/2026-09-29-io-design.md."

Error subclass: #FileSystemError.
FileSystemError subclass: #FileDoesNotExist.
FileSystemError subclass: #FileAlreadyExists.
Error subclass: #NetworkError.
NetworkError subclass: #ConnectionRefused.
NetworkError subclass: #ConnectionTimedOut.
NetworkError subclass: #NameLookupFailure.
NetworkError subclass: #BodyTooLarge.
Error subclass: #LineTooLong.
Error subclass: #OSProcessError.

"-- Streams over a descriptor (standard streams, files, sockets) -----------"

Object subclass: #IOStream instanceVariableNames: 'fd closed'.

IOStream class >> onDescriptor: anInteger
  ^ self basicNew setDescriptor: anInteger.

IOStream >> setDescriptor: anInteger
  fd := anInteger.
  closed := false.
  ^ self.

IOStream >> descriptor
  ^ fd.

IOStream >> isClosed
  ^ closed.

IOStream >> nextLine
  ^ self __fdReadLine: fd.

IOStream >> nextLineMax: aByteCount
  "nextLine, for input that is not trusted: a line longer than aByteCount
   bytes raises LineTooLong instead of growing without bound."
  ^ self __fdReadLine: fd max: aByteCount.

IOStream >> upToEnd
  ^ self __fdReadAll: fd.

IOStream >> contents
  ^ self upToEnd.

IOStream >> next
  "The next character (a whole UTF-8 character), or nil at the end."
  | s |
  s := self __fdReadChars: fd count: 1.
  ^ s isNil ifTrue: [nil] ifFalse: [s at: 1].

IOStream >> next: anInteger
  "Up to anInteger characters (fewer at the end), or nil at the end."
  ^ self __fdReadChars: fd count: anInteger.

IOStream >> nextByteCount: anInteger
  "Up to anInteger bytes decoded as UTF-8 text (for protocols that count
   bytes, like HTTP's Content-Length)."
  ^ self __fdRead: fd count: anInteger binary: false.

IOStream >> nextBytes: anInteger
  ^ self __fdRead: fd count: anInteger binary: true.

IOStream >> atEnd
  ^ self __fdAtEnd: fd.

IOStream >> linesDo: aBlock
  | line |
  [(line := self nextLine) notNil] whileTrue: [aBlock value: line].
  ^ self.

IOStream >> lines
  | out |
  out := OrderedCollection new.
  self linesDo: [:each | out add: each].
  ^ out asArray.

IOStream >> nextPutAll: aString
  self __fdWrite: fd data: aString.
  ^ aString.

IOStream >> nextPutBytes: anArray
  self __fdWrite: fd data: anArray.
  ^ anArray.

IOStream >> nextPut: aCharacter
  self __fdWrite: fd data: aCharacter asString.
  ^ aCharacter.

IOStream >> show: anObject
  self nextPutAll: anObject displayString.
  ^ self.

IOStream >> print: anObject
  self nextPutAll: anObject printString.
  ^ self.

IOStream >> display: anObject
  self nextPutAll: anObject displayString.
  ^ self.

IOStream >> << anObject
  self nextPutAll: (anObject isString ifTrue: [anObject] ifFalse: [anObject displayString]).
  ^ self.

IOStream >> cr
  self nextPutAll: (String with: Character lf).
  ^ self.

IOStream >> lf
  ^ self cr.

IOStream >> nl
  ^ self cr.

IOStream >> crlf
  self nextPutAll: (String with: Character cr with: Character lf).
  ^ self.

IOStream >> space
  self nextPutAll: ' '.
  ^ self.

IOStream >> tab
  self nextPutAll: (String with: Character tab).
  ^ self.

IOStream >> showCr: anObject
  self show: anObject; cr.
  ^ self.

IOStream >> flush
  self __fdFlush: fd.
  ^ self.

IOStream >> timeout: milliseconds
  "Reads and writes wait at most this long, then signal ConnectionTimedOut
   (nil: no limit)."
  self __fdTimeout: fd ms: milliseconds.
  ^ self.

IOStream >> close
  closed ifFalse: [closed := true. self __fdClose: fd].
  ^ self.

IOStream >> printOn: aStream
  aStream nextPutAll: 'an IOStream on descriptor ' , fd printString.

"-- Standard streams ---------------------------------------------------------"

Object subclass: #Stdio.

Stdio class >> stdin
  ^ IOStream onDescriptor: 0.

Stdio class >> stdout
  ^ IOStream onDescriptor: 1.

Stdio class >> stderr
  ^ IOStream onDescriptor: 2.

"-- Files and directories ----------------------------------------------------"

Object subclass: #FileReference instanceVariableNames: 'path'.

FileReference class >> path: aString
  ^ self basicNew setPath: aString.

FileReference >> setPath: aString
  path := aString.
  ^ self.

FileReference >> pathString
  ^ path.

FileReference >> fullName
  ^ self __fileAbsolute: path.

FileReference >> asFileReference
  ^ self.

FileReference >> stat
  ^ self __fileStat: path.

FileReference >> statOrSignal
  | s |
  s := self __fileStat: path.
  s isNil ifTrue: [^ FileDoesNotExist signal: 'no such file or directory: ' , path].
  ^ s.

FileReference >> exists
  ^ (self __fileStat: path) notNil.

FileReference >> isFile
  | s |
  s := self __fileStat: path.
  ^ s notNil and: [s at: 1].

FileReference >> isDirectory
  | s |
  s := self __fileStat: path.
  ^ s notNil and: [s at: 2].

FileReference >> size
  ^ self statOrSignal at: 3.

FileReference >> modificationTime
  "Milliseconds since the Unix epoch."
  ^ self statOrSignal at: 4.

FileReference >> isReadable
  | s |
  s := self __fileStat: path.
  ^ s notNil and: [s at: 5].

FileReference >> isWritable
  | s |
  s := self __fileStat: path.
  ^ s notNil and: [s at: 6].

FileReference >> basename
  | p i |
  p := self fullName.
  i := p size.
  [i > 0 and: [(p at: i) ~= $/]] whileTrue: [i := i - 1].
  ^ p copyFrom: i + 1 to: p size.

FileReference >> extension
  | b i |
  b := self basename.
  i := b size.
  [i > 1 and: [(b at: i) ~= $.]] whileTrue: [i := i - 1].
  ^ i > 1 ifTrue: [b copyFrom: i + 1 to: b size] ifFalse: [''].

FileReference >> basenameWithoutExtension
  | b e |
  b := self basename.
  e := self extension.
  ^ e isEmpty ifTrue: [b] ifFalse: [b copyFrom: 1 to: b size - e size - 1].

FileReference >> parent
  | p i |
  p := self fullName.
  i := p size.
  [i > 1 and: [(p at: i) ~= $/]] whileTrue: [i := i - 1].
  ^ FileReference path: (i <= 1 ifTrue: ['/'] ifFalse: [p copyFrom: 1 to: i - 1]).

FileReference >> / aString
  ^ FileReference path: ((path endsWith: '/') ifTrue: [path , aString] ifFalse: [path , '/' , aString]).

FileReference >> children
  ^ (self __dirList: path) collect: [:name | self / name].

FileReference >> files
  ^ self children select: [:each | each isFile].

FileReference >> directories
  ^ self children select: [:each | each isDirectory].

FileReference >> contents
  ^ self __fileRead: path binary: false.

FileReference >> contents: aString
  self __fileWrite: path data: aString append: false.
  ^ self.

FileReference >> appendContents: aString
  self __fileWrite: path data: aString append: true.
  ^ self.

FileReference >> binaryContents
  ^ self __fileRead: path binary: true.

FileReference >> binaryContents: anArray
  self __fileWrite: path data: anArray append: false.
  ^ self.

FileReference >> lines
  ^ self readStreamDo: [:s | s lines].

FileReference >> linesDo: aBlock
  self readStreamDo: [:s | s linesDo: aBlock].
  ^ self.

FileReference >> readStream
  self isDirectory ifTrue: [^ FileSystemError signal: 'is a directory: ' , path].
  ^ IOStream onDescriptor: (self __fileOpen: path mode: 'r').

FileReference >> writeStream
  ^ IOStream onDescriptor: (self __fileOpen: path mode: 'w').

FileReference >> appendStream
  ^ IOStream onDescriptor: (self __fileOpen: path mode: 'a').

FileReference >> readStreamDo: aBlock
  | s |
  s := self readStream.
  ^ [aBlock value: s] ensure: [s close].

FileReference >> writeStreamDo: aBlock
  | s |
  s := self writeStream.
  ^ [aBlock value: s] ensure: [s close].

FileReference >> appendStreamDo: aBlock
  | s |
  s := self appendStream.
  ^ [aBlock value: s] ensure: [s close].

FileReference >> createFile
  self exists ifFalse: [self __fileWrite: path data: '' append: true].
  ^ self.

FileReference >> createDirectory
  self __dirCreate: path parents: false.
  ^ self.

FileReference >> ensureCreateDirectory
  self __dirCreate: path parents: true.
  ^ self.

FileReference >> delete
  self exists ifFalse: [^ FileDoesNotExist signal: 'no such file or directory: ' , path].
  self __fileDelete: path recursive: false.
  ^ self.

FileReference >> ensureDelete
  self __fileDelete: path recursive: false.
  ^ self.

FileReference >> deleteAll
  self __fileDelete: path recursive: true.
  ^ self.

FileReference >> copyTo: aFileReference
  self __fileCopy: path to: aFileReference asFileReference pathString.
  ^ aFileReference asFileReference.

FileReference >> moveTo: aFileReference
  self __fileMove: path to: aFileReference asFileReference pathString.
  ^ aFileReference asFileReference.

FileReference >> renameTo: aString
  ^ self moveTo: self parent / aString.

FileReference >> = other
  ^ (other isKindOf: FileReference) and: [self fullName = other fullName].

FileReference >> hash
  ^ self fullName hash.

FileReference >> printOn: aStream
  aStream nextPutAll: 'File @ ' , path.

String >> asFileReference
  ^ FileReference path: self.

Object subclass: #File.

File class >> named: aString
  ^ FileReference path: aString.

Object subclass: #FileSystem.

FileSystem class >> workingDirectory
  ^ FileReference path: self __osCwd.

FileSystem class >> home
  "HOME; on Windows, USERPROFILE when HOME is unset."
  | h |
  h := Smalltalk getenv: 'HOME'.
  (h isNil and: [Smalltalk platform = 'windows']) ifTrue: [h := Smalltalk getenv: 'USERPROFILE'].
  ^ FileReference path: h.

FileSystem class >> temp
  ^ FileReference path: self __fileTempDir.

FileSystem class >> root
  ^ FileReference path: '/'.

"-- The program and its environment -----------------------------------------"

SmalltalkImage >> arguments
  "The script's own arguments, after its path on the command line."
  ^ self __osArguments.

SmalltalkImage >> programPath
  ^ self __osProgramPath.

SmalltalkImage >> getenv: aString
  ^ self __osGetenv: aString.

SmalltalkImage >> setenv: aString to: aValue
  self __osSetenv: aString to: aValue.
  ^ aValue.

SmalltalkImage >> environment
  | d |
  d := Dictionary new.
  self __osEnvironment do: [:entry |
    | i |
    i := entry indexOf: $=.
    i > 0 ifTrue: [d at: (entry copyFrom: 1 to: i - 1) put: (entry copyFrom: i + 1 to: entry size)]].
  ^ d.

SmalltalkImage >> exit: anInteger
  "Ends the program now with this exit status, after flushing output."
  ^ self __osExit: anInteger.

SmalltalkImage >> quit
  ^ self exit: 0.

SmalltalkImage >> pid
  ^ self __osPid.

SmalltalkImage >> hostName
  ^ self __osHostName.

SmalltalkImage >> platform
  ^ self __osPlatform.

SmalltalkImage >> workingDirectory
  ^ FileSystem workingDirectory.

SmalltalkImage >> changeDirectory: aPath
  self __osChdir: aPath asFileReference pathString.
  ^ self workingDirectory.

"-- Other programs -------------------------------------------------------------"

Object subclass: #OSProcessResult instanceVariableNames: 'exitCode output errorOutput'.

OSProcessResult >> setExitCode: anInteger output: out errorOutput: err
  exitCode := anInteger.
  output := out.
  errorOutput := err.
  ^ self.

OSProcessResult >> exitCode
  ^ exitCode.

OSProcessResult >> output
  ^ output.

OSProcessResult >> errorOutput
  ^ errorOutput.

OSProcessResult >> succeeded
  ^ exitCode = 0.

OSProcessResult >> printOn: aStream
  aStream nextPutAll: 'an OSProcessResult (exit ' , exitCode printString , ')'.

Object subclass: #OSProcess.

OSProcess class >> run: aCommand
  ^ self run: aCommand arguments: #() input: nil.

OSProcess class >> run: aCommand arguments: anArray
  ^ self run: aCommand arguments: anArray input: nil.

OSProcess class >> run: aCommand arguments: anArray input: aStringOrNil
  | r |
  r := self __osRun: (Array with: aCommand) , anArray asArray input: aStringOrNil.
  ^ OSProcessResult new setExitCode: (r at: 1) output: (r at: 2) errorOutput: (r at: 3).

OSProcess class >> shell: aString
  ^ self shell: aString input: nil.

OSProcess class >> shell: aString input: input
  "The system's shell runs aString: /bin/sh -c, and cmd.exe /c on Windows."
  ^ Smalltalk platform = 'windows'
      ifTrue: [self run: 'cmd.exe' arguments: (Array with: '/c' with: aString) input: input]
      ifFalse: [self run: '/bin/sh' arguments: (Array with: '-c' with: aString) input: input].

OSProcess class >> command: aString
  "The output of a shell command line, without its final newline; signals
   OSProcessError when the command fails."
  | r out |
  r := self shell: aString.
  r succeeded ifFalse: [^ OSProcessError signal: aString , ' exited with ' , r exitCode printString , ': ' , r errorOutput].
  out := r output.
  out := (out notEmpty and: [out last = Character lf]) ifTrue: [out copyFrom: 1 to: out size - 1] ifFalse: [out].
  "On Windows the shell ends a line with CR LF."
  ^ (Smalltalk platform = 'windows' and: [out notEmpty and: [out last = Character cr]])
      ifTrue: [out copyFrom: 1 to: out size - 1] ifFalse: [out].

OSProcess class >> spawn: aCommand arguments: anArray
  "Starts a program that shares our standard streams; answers its pid."
  ^ self __osSpawn: (Array with: aCommand) , anArray asArray.

OSProcess class >> waitFor: aPid
  "Waits for a spawned program to end; answers its exit status."
  ^ self __osWaitPid: aPid.

OSProcess class >> kill: aPid
  ^ self kill: aPid signal: 15.

OSProcess class >> kill: aPid signal: anInteger
  self __osKill: aPid signal: anInteger.
  ^ aPid.
