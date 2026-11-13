"EXPECT-ERROR: BinaryFile is closed"
"BinaryFile: linkCount after close signals Error, as every use but close does."
| dir f |
dir := FileSystem temp / ('protost-binfile-links-closed-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[f := BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create).
 f close] ensure: [dir deleteAll].
f linkCount.
