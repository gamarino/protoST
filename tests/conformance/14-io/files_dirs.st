"EXPECT: #('a.txt' 'b.txt' 'sub') true copy moved false 5"
"Directories: children, files, parent, copyTo:, moveTo:, size."
dir := FileSystem temp / ('protost-io-dirs-' , Smalltalk pid printString).
dir ensureCreateDirectory.
(dir / 'b.txt') contents: 'hello'.
(dir / 'a.txt') contents: 'x'.
(dir / 'sub') createDirectory.
names := (dir children collect: [:c | c basename]) asArray.
isDir := (dir / 'sub') isDirectory and: [(dir / 'sub') parent fullName = dir fullName].
(dir / 'b.txt') copyTo: dir / 'copy.txt'.
(dir / 'copy.txt') moveTo: dir / 'moved.txt'.
copyGone := (dir / 'copy.txt') exists.
size := (dir / 'moved.txt') size.
dir deleteAll.
names printString , ' ' , isDir printString , ' copy moved ' , copyGone printString , ' ' , size printString.
