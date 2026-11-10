"EXPECT-ERROR: BinaryFile is closed"
"BinaryFile: a read after close signals Error."
| dir f |
dir := FileSystem temp / ('protost-binfile-closed-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[f := BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create).
 f append: #[1 2 3].
 f close] ensure: [dir deleteAll].
f readAt: 0 size: 3.
