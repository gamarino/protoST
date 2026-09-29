"EXPECT: é 1"
"A chunked body is decoded as bytes: a character split across two chunks
 arrives whole."
Import from: 'http'.
Object subclass: #RawServer.
RawServer >> answer: srv
  | c crlf |
  c := srv accept.
  crlf := String with: Character cr with: Character lf.
  [(c nextLine ifNil: ['']) notEmpty] whileTrue: [].
  c nextPutAll: 'HTTP/1.1 200 OK' , crlf , 'Transfer-Encoding: chunked' , crlf , crlf.
  "1\r\n<C3>\r\n1\r\n<A9>\r\n0\r\n\r\n: the two bytes of U+00E9 in two chunks"
  c nextPutBytes: #(49 13 10 195 13 10 49 13 10 169 13 10 48 13 10 13 10).
  c flush; close.
  ^ nil.
srv := ServerSocket listenOn: 0 host: '127.0.0.1'.
f := RawServer new asActor answer: srv.
r := HTTPClient get: 'http://127.0.0.1:' , srv port printString , '/'.
f wait.
srv close.
r body , ' ' , r body size printString.
