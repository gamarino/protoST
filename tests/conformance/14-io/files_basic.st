"EXPECT: 3 gamma true notes.txt txt false"
"Files: write, append, read lines, names; delete a directory tree."
dir := FileSystem temp / ('protost-io-basic-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'notes.txt'.
f contents: 'alpha' , (String with: Character lf) , 'beta'.
f appendContents: (String with: Character lf) , 'gamma'.
lines := f lines.
result := lines size printString , ' ' , (lines at: 3) , ' ' , f exists printString , ' ' , f basename , ' ' , f extension.
dir deleteAll.
result , ' ' , dir exists printString.
