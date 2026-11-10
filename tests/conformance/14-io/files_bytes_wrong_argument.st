"EXPECT: true true true"
"binaryContents: / contents: with something that is neither text nor bytes fail (with no deprecation warning)."
dir := FileSystem temp / ('protost-io-bytes-wrong-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'b.bin'.
bad := [:value | [f binaryContents: value. false] on: Error do: [:e | true]].
r := (bad value: nil) printString, ' ', (bad value: 42) printString, ' ', (bad value: #(1 300)) printString.
dir deleteAll.
r.
