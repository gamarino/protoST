"EXPECT: 5 5 3 5"
"Text read from a file keeps a NUL: contents, nextLine and the whole-stream
 read all answer the full length."
nul := Character value: 0.
dir := FileSystem temp / ('protost-io-nul-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 't.txt'.
f contents: 'ab' , (String with: nul) , 'cd'.
whole := f contents.
line := nil.
f readStreamDo: [:s | line := s nextLine].
f contents: 'x' , (String with: nul) , 'y' , (String with: Character lf) , 'z'.
lineTwo := nil.
f readStreamDo: [:s | lineTwo := s nextLine].
all := nil.
f readStreamDo: [:s | all := s upToEnd].
dir deleteAll.
whole size printString, ' ', line size printString, ' ', lineTwo size printString, ' ',
  all size printString.
