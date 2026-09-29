"EXPECT: rejected rejected rejected 500 nil"
"Line breaks in a request line or a header, from the program or from a handler,
 never reach the wire: they could forge extra headers or requests."
Import from: 'http'.
crlf := String with: Character cr with: Character lf.
try := [:blk | [blk value. 'sent'] on: Error do: [:e |
  (e messageText includesSubstring: 'invalid') ifTrue: ['rejected'] ifFalse: [e messageText]]].
a := try value: [HTTPClient get: 'http://127.0.0.1:9/b HTTP/1.1' , crlf , 'X-Injected: 1'].
b := try value: [HTTPClient request: 'GET' url: 'http://127.0.0.1:9/'
  headers: (Dictionary new at: 'x-a' put: 'v' , crlf , 'X-Evil: yes'; yourself) body: nil].
c := try value: [HTTPClient request: 'GE T' url: 'http://127.0.0.1:9/' headers: nil body: nil].
server := HTTPServer on: 0 handler: [:req |
  (HTTPResponse ok: 'x') headerAt: 'x-a' put: 'v' , crlf , 'Set-Cookie: evil'; yourself].
server startInBackground.
r := HTTPClient get: 'http://127.0.0.1:' , server port printString , '/'.
server stop.
a , ' ' , b , ' ' , c , ' ' , r status printString , ' ' , (r headerAt: 'set-cookie') printString.
