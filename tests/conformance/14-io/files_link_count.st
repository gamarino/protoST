"EXPECT: 1 true FileDoesNotExist"
"FileReference>>linkCount: the hard links of a file by path (a fresh file has
 one); a directory answers a count too; a missing path signals
 FileDoesNotExist, as size and modificationTime do."
| dir f one dirOk missing |
dir := FileSystem temp / ('protost-files-links-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[f := dir / 'one.txt'.
 f contents: 'x'.
 one := f linkCount.
 dirOk := dir linkCount >= 1.
 missing := [(dir / 'missing') linkCount. 'no error'] on: FileDoesNotExist do: [:e | 'FileDoesNotExist']]
  ensure: [dir deleteAll].
one printString , ' ' , dirOk printString , ' ' , missing.
