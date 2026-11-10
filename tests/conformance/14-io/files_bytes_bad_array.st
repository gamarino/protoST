"EXPECT-ERROR: bytes are integers from 0 to 255"
"An Array holding a non-byte is refused."
dir := FileSystem temp / ('protost-io-bytes-bad-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[(dir / 'b.bin') binaryContents: #(0 300)] ensure: [dir deleteAll].
