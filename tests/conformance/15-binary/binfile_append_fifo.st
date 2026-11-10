"EXPECT: FileSystemError BinaryFile>>append: true"
"BinaryFile append: needs a regular file: on a FIFO it signals FileSystemError,
 named by the selector, before writing anything (protoIO 0.4.0 refuses a
 descriptor that is not a regular file: its end cannot be read, so the offset
 append: answers would be meaningless). The FIFO is opened for reading and
 writing, which does not wait for another end. mkfifo is POSIX: on Windows
 the test reports SKIP."
| dir path f result |
Smalltalk platform = 'windows' ifTrue: [^ 'SKIP: no FIFOs on Windows'].
dir := FileSystem temp / ('protost-binfile-fifo-' , Smalltalk pid printString).
dir ensureCreateDirectory.
path := (dir / 'pipe') pathString.
[OSProcess run: 'mkfifo' arguments: {path}.
 f := BinaryFile open: path options: #(#read #write).
 result := [f append: #[1 2 3]. 'no error']
   on: FileSystemError do: [:e | 'FileSystemError ', ((e messageText copyFrom: 1 to: 20) = 'BinaryFile>>append: ' ifTrue: ['BinaryFile>>append:'] ifFalse: [e messageText]), ' ', (e messageText includesSubstring: 'regular') printString].
 f close.
 result]
  ensure: [dir deleteAll]
