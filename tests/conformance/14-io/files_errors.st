"EXPECT: missing FileDoesNotExist exists"
"File errors are classed and catchable."
dir := FileSystem temp / ('protost-io-errors-' , Smalltalk pid printString).
dir ensureCreateDirectory.
missing := [(dir / 'nope.txt') contents. 'read'] on: FileDoesNotExist do: [:e | 'missing'].
cls := [(dir / 'nope.txt') contents] on: Error do: [:e | e class name].
(dir / 'a') createDirectory.
again := [(dir / 'a') createDirectory. 'created twice'] on: FileAlreadyExists do: [:e | 'exists'].
dir deleteAll.
missing , ' ' , cls , ' ' , again.
