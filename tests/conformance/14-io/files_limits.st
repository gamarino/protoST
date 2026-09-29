"EXPECT: LineTooLong short refused true"
"nextLineMax: bounds a line from untrusted input; a negative count is refused;
 fullName folds '..' lexically and keeps a symbolic link as written."
dir := FileSystem temp / ('protost-io-limits-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'lines.txt'.
f contents: (String new: 100 withAll: $x) , (String with: Character lf) , 'short' , (String with: Character lf).
long := f readStreamDo: [:s | [s nextLineMax: 50] on: LineTooLong do: [:e | e class name]].
short := f readStreamDo: [:s | s nextLine. s nextLineMax: 50].
negative := f readStreamDo: [:s | [s next: -1. 'read'] on: Error do: [:e | 'refused']].
OSProcess run: 'ln' arguments: (Array with: '-s' with: dir fullName with: (dir / 'link') fullName).
kept := ((dir / 'link' / 'x' / '..' / 'lines.txt') fullName) = ((dir fullName) , '/link/lines.txt').
dir deleteAll.
long displayString , ' ' , short , ' ' , negative , ' ' , kept printString.
