"EXPECT: 20000 200010000 true 100000 after a|b|c|last x y cdef z false tail nil LineTooLong 0123456789 first"
"nextLine answers a line protoIO's read-ahead buffer already holds without
 reading, and reads only when the buffer lacks one. Lines across many 64 KiB
 refills (alternating LF and CRLF), a line longer than the buffer, CRLF and a
 last line without a newline, nextLine interleaved with next:, atEnd and
 upToEnd on one stream, and nextLineMax: refusing a buffered line while
 leaving the stream where it was."
dir := FileSystem temp / ('protost-io-lines-' , Smalltalk pid printString).
dir ensureCreateDirectory.
pad := String new: 40 withAll: $p.
many := dir / 'many.txt'.
many writeStreamDo: [:s |
  1 to: 20000 do: [:i |
    s nextPutAll: 'line-' , i printString , '-' , pad.
    i odd ifTrue: [s crlf] ifFalse: [s lf]]].
count := 0. sum := 0. clean := true.
many linesDo: [:line | | n |
  count := count + 1.
  n := (line copyFrom: 6 to: (line indexOf: $- startingAt: 6) - 1) asNumber.
  sum := sum + n.
  n = count ifFalse: [clean := false].
  line = ('line-' , n printString , '-' , pad) ifFalse: [clean := false]].
lf := String with: Character lf.
long := dir / 'long.txt'.
long contents: (String new: 100000 withAll: $q) , lf , 'after'.
longLines := long readStreamDo: [:s | s nextLine size printString , ' ' , s nextLine].
mixed := dir / 'mixed.txt'.
mixed writeStreamDo: [:s | s nextPutAll: 'a'; crlf; nextPutAll: 'b'; lf; nextPutAll: 'c'; crlf; nextPutAll: 'last'].
joined := mixed lines inject: '' into: [:acc :each | acc isEmpty ifTrue: [each] ifFalse: [acc , '|' , each]].
inter := dir / 'inter.txt'.
inter contents: 'x' , lf , 'y' , lf , 'cdefz' , lf , 'tail'.
interleaved := inter readStreamDo: [:s |
  s nextLine , ' ' , s nextLine , ' ' , (s next: 4) , ' ' , s nextLine , ' ' ,
    s atEnd printString , ' ' , s upToEnd , ' ' , s nextLine printString].
limited := dir / 'limited.txt'.
limited contents: 'first' , lf , '0123456789' , lf , 'next'.
refused := limited readStreamDo: [:s | | first e |
  first := s nextLine.
  e := [s nextLineMax: 4] on: LineTooLong do: [:ex | ex class name].
  e displayString , ' ' , s nextLine , ' ' , first].
dir deleteAll.
count printString , ' ' , sum printString , ' ' , clean printString , ' ' , longLines , ' ' , joined , ' ' , interleaved , ' ' , refused.
