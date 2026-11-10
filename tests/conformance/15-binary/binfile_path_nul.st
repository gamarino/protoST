"EXPECT-ERROR: BinaryFile class>>open:options: the path must not contain NUL"
"BinaryFile: a path with a NUL is refused, never cut at the NUL (the OS reads
 a path up to its first NUL, so 'victim', NUL, '.evil' would open 'victim')."
| dir p |
dir := FileSystem temp / ('protost-binfile-nul-' , Smalltalk pid printString).
dir ensureCreateDirectory.
p := (dir / 'victim') pathString , (String with: (Character value: 0)) , '.evil'.
[BinaryFile open: p options: #(#write #create)] ensure: [
  (dir / 'victim') exists ifTrue: ['FAIL: the truncated path was created' displayNl].
  dir deleteAll].
