"EXPECT: #[0 1 255]"
"An Array of 0-255 is still accepted for one release (a warning goes to stderr)."
dir := FileSystem temp / ('protost-io-bytes-dep-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'b.bin'.
f binaryContents: #(0 1 255).
back := f binaryContents.
dir deleteAll.
back printString.
