"EXPECT-ERROR: opened to append"
"BinaryFile: writeAt:bytes: on a file opened with #append is refused (an append descriptor would ignore the offset)."
dir := FileSystem temp / ('protost-binfile-append-writeat-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f |
 f := BinaryFile open: (dir / 'seg') pathString options: #(#append #create).
 [f writeAt: 0 bytes: #[1]] ensure: [f close]]
  ensure: [dir deleteAll].
