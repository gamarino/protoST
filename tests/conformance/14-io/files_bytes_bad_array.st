"EXPECT-ERROR: binaryContents: an Array is not bytes"
"An Array holding a non-byte is refused as an Array: since 0.10.0 no Array is bytes."
dir := FileSystem temp / ('protost-io-bytes-bad-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[(dir / 'b.bin') binaryContents: #(0 300)] ensure: [dir deleteAll].
