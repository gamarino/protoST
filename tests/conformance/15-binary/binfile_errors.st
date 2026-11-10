"EXPECT-ERROR: FileAlreadyExists"
"BinaryFile: #exclusive refuses a file that already exists."
dir := FileSystem temp / ('protost-binfile-errors-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| first |
 first := BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create #exclusive).
 first close.
 BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create #exclusive)]
  ensure: [dir deleteAll].
