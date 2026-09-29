"EXPECT: 400 400 431 413"
"The server refuses malformed or oversized requests before reading them:
 a bad Content-Length, a header line longer than 8 KiB, a body over maxBodySize."
Import from: 'http'.
server := HTTPServer on: 0 handler: [:req | 'ok'].
server startInBackground.
crlf := String with: Character cr with: Character lf.
statusOf := [:text | | s line |
  s := Socket connectTo: '127.0.0.1' port: server port.
  s nextPutAll: text; flush.
  line := s nextLine.
  s close.
  line isNil ifTrue: ['closed'] ifFalse: [(line substrings: ' ') at: 2]].
a := statusOf value: 'POST / HTTP/1.1' , crlf , 'content-length: -5' , crlf , crlf.
b := statusOf value: 'POST / HTTP/1.1' , crlf , 'content-length: 12abc' , crlf , crlf.
c := statusOf value: 'GET / HTTP/1.1' , crlf , 'x-long: ' , (String new: 9000 withAll: $a) , crlf , crlf.
d := statusOf value: 'POST / HTTP/1.1' , crlf , 'content-length: 999999999999' , crlf , crlf.
server stop.
a , ' ' , b , ' ' , c , ' ' , d.
