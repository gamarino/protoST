"EXPECT: é ab ñandú on port true"
"Streams count characters, not bytes: next and next: never split a UTF-8
 character; a server prints the port it actually bound; opening a
 directory as a file signals instead of leaking a descriptor."
Import from: 'http'.
dir := FileSystem temp / ('protost-io-utf8-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'u.txt'.
f contents: 'éabñandú'.
first := nil. two := nil. rest := nil.
f readStreamDo: [:s | first := s next. two := s next: 2. rest := s upToEnd].
server := HTTPServer on: 0 handler: [:r | 'x'].
shown := server port > 0 and: [(server printString includesSubstring: ' 0') not].
refused := [dir readStream. false] on: FileSystemError do: [:e | true].
server stop.
dir deleteAll.
first asString , ' ' , two , ' ' , rest , ' on port ' , (shown and: [refused]) printString.
