"-- http.st: an HTTP/1.1 client and server --

USAGE (client)
  Import from: 'http'.
  r := HTTPClient get: 'https://api.github.com/repos/gamarino/protoST'.
  r status.                                => 200
  (r json at: 'name').                     => 'protoST'
  HTTPClient post: 'http://localhost:8080/items' json: (Dictionary new at: 'n' put: 1; yourself).
  HTTPClient request: 'PUT' url: url headers: (Dictionary new at: 'X-Token' put: 't'; yourself) body: 'x'.

USAGE (server)
  server := HTTPServer on: 8080 handler: [:request |
    request path = '/state'
      ifTrue: [HTTPResponse json: twin state]
      ifFalse: [HTTPResponse notFound]].
  server start.                            serves on this thread until `stop`
  server startInBackground.                serves on an actor; answers the server

The handler answers an HTTPResponse, a String (200, text/plain), nil (204),
or any other object (200, its JSON). Each connection is handled by its own
actor; an error in the handler answers 500 with the error's text. A request
answers method, path, query (a Dictionary), headers (lower-case keys), body
and json.

http and https (TLS with certificate and host-name verification), chunked
bodies, redirects (up to 5), `HTTPClient timeout:` (milliseconds, default
30000). One request per connection (Connection: close); no HTTP/2, no
WebSockets, no TLS server."

Import from: 'net'.
Import from: 'json'.

"-- Messages -------------------------------------------------------------------"

Object subclass: #HTTPResponse instanceVariableNames: 'status reason headers body'.

HTTPResponse class >> status: aCode body: aString
  ^ self new setStatus: aCode body: aString.

HTTPResponse class >> ok: aString
  ^ self status: 200 body: aString.

HTTPResponse class >> json: anObject
  ^ (self status: 200 body: (JSON stringify: anObject)) contentType: 'application/json'.

HTTPResponse class >> notFound
  ^ self status: 404 body: 'Not Found'.

HTTPResponse class >> noContent
  ^ self status: 204 body: ''.

HTTPResponse class >> reasonFor: aCode
  aCode = 200 ifTrue: [^ 'OK'].
  aCode = 201 ifTrue: [^ 'Created'].
  aCode = 204 ifTrue: [^ 'No Content'].
  aCode = 301 ifTrue: [^ 'Moved Permanently'].
  aCode = 302 ifTrue: [^ 'Found'].
  aCode = 304 ifTrue: [^ 'Not Modified'].
  aCode = 400 ifTrue: [^ 'Bad Request'].
  aCode = 401 ifTrue: [^ 'Unauthorized'].
  aCode = 403 ifTrue: [^ 'Forbidden'].
  aCode = 404 ifTrue: [^ 'Not Found'].
  aCode = 405 ifTrue: [^ 'Method Not Allowed'].
  aCode = 500 ifTrue: [^ 'Internal Server Error'].
  aCode = 503 ifTrue: [^ 'Service Unavailable'].
  ^ 'Status'.

HTTPResponse >> setStatus: aCode body: aString
  status := aCode.
  reason := HTTPResponse reasonFor: aCode.
  headers := Dictionary new.
  headers at: 'content-type' put: 'text/plain; charset=utf-8'.
  body := aString.
  ^ self.

HTTPResponse >> setStatus: aCode reason: aString headers: aDictionary body: aBody
  status := aCode.
  reason := aString.
  headers := aDictionary.
  body := aBody.
  ^ self.

HTTPResponse >> status
  ^ status.

HTTPResponse >> reason
  ^ reason.

HTTPResponse >> headers
  ^ headers.

HTTPResponse >> headerAt: aName
  ^ headers at: aName asLowercase ifAbsent: [nil].

HTTPResponse >> headerAt: aName put: aValue
  headers at: aName asLowercase put: aValue.
  ^ self.

HTTPResponse >> contentType
  ^ self headerAt: 'content-type'.

HTTPResponse >> contentType: aString
  ^ self headerAt: 'content-type' put: aString.

HTTPResponse >> body
  ^ body.

HTTPResponse >> contents
  ^ body.

HTTPResponse >> json
  ^ JSON parse: body.

HTTPResponse >> isSuccess
  ^ status between: 200 and: 299.

HTTPResponse >> printOn: aStream
  aStream nextPutAll: 'an HTTPResponse (' , status printString , ' ' , reason , ')'.

Object subclass: #HTTPRequest instanceVariableNames: 'method path query headers body peer'.

HTTPRequest >> setMethod: m path: p query: q headers: h body: b peer: aString
  method := m.
  path := p.
  query := q.
  headers := h.
  body := b.
  peer := aString.
  ^ self.

HTTPRequest >> method
  ^ method.

HTTPRequest >> path
  ^ path.

HTTPRequest >> query
  ^ query.

HTTPRequest >> headers
  ^ headers.

HTTPRequest >> headerAt: aName
  ^ headers at: aName asLowercase ifAbsent: [nil].

HTTPRequest >> body
  ^ body.

HTTPRequest >> json
  ^ JSON parse: body.

HTTPRequest >> peer
  ^ peer.

HTTPRequest >> printOn: aStream
  aStream nextPutAll: 'an HTTPRequest (' , method , ' ' , path , ')'.

"-- Shared helpers ---------------------------------------------------------------"

Object subclass: #HTTP.

HTTP class >> hexValue: aString
  | n |
  n := 0.
  aString do: [:c |
    | d |
    d := '0123456789abcdef' indexOf: c asLowercase.
    d = 0 ifTrue: [^ n].
    n := n * 16 + d - 1].
  ^ n.

HTTP class >> percentDecode: aString
  | out i |
  out := WriteStream on: String new.
  i := 1.
  [i <= aString size] whileTrue: [
    | c |
    c := aString at: i.
    (c = $% and: [i + 2 <= aString size])
      ifTrue: [out nextPut: (self hexValue: (aString copyFrom: i + 1 to: i + 2)) asCharacter. i := i + 3]
      ifFalse: [out nextPut: (c = $+ ifTrue: [Character space] ifFalse: [c]). i := i + 1]].
  ^ out contents.

HTTP class >> parseQuery: aString
  | d |
  d := Dictionary new.
  (aString isNil or: [aString isEmpty]) ifTrue: [^ d].
  (aString substrings: '&') do: [:pair |
    | i |
    i := pair indexOf: $=.
    i = 0
      ifTrue: [d at: (self percentDecode: pair) put: '']
      ifFalse: [d at: (self percentDecode: (pair copyFrom: 1 to: i - 1))
                  put: (self percentDecode: (pair copyFrom: i + 1 to: pair size))]].
  ^ d.

HTTP class >> readHeadersFrom: aSocket
  "Header lines up to the blank line: a Dictionary with lower-case names."
  | d line |
  d := Dictionary new.
  [(line := aSocket nextLine) notNil and: [line notEmpty]] whileTrue: [
    | i |
    i := line indexOf: $:.
    i > 0 ifTrue: [d at: (line copyFrom: 1 to: i - 1) trimBoth asLowercase
                     put: (line copyFrom: i + 1 to: line size) trimBoth]].
  ^ d.

HTTP class >> readBodyFrom: aSocket headers: aDictionary
  | te len |
  te := aDictionary at: 'transfer-encoding' ifAbsent: [''].
  (te asLowercase includesSubstring: 'chunked') ifTrue: [^ self readChunkedFrom: aSocket].
  len := aDictionary at: 'content-length' ifAbsent: [nil].
  len isNil ifFalse: [
    len := len asNumber.
    len = 0 ifTrue: [^ ''].
    ^ (aSocket next: len) ifNil: ['']].
  ^ ''.

HTTP class >> readChunkedFrom: aSocket
  | out size |
  out := WriteStream on: String new.
  [size := self hexValue: ((aSocket nextLine ifNil: ['0']) substrings: ';') first trimBoth.
   size > 0] whileTrue: [
    out nextPutAll: (aSocket next: size).
    aSocket nextLine].
  [(aSocket nextLine ifNil: ['']) notEmpty] whileTrue: [].
  ^ out contents.

HTTP class >> write: aBody headers: aDictionary to: aSocket
  aDictionary keysAndValuesDo: [:k :v |
    aSocket nextPutAll: k , ': ' , v displayString; crlf].
  aSocket nextPutAll: 'content-length: ' , (self __byteSize: aBody) printString; crlf.
  aSocket nextPutAll: 'connection: close'; crlf; crlf.
  aSocket nextPutAll: aBody.
  aSocket flush.

"-- Client -------------------------------------------------------------------------"

Object subclass: #HTTPClient.

HTTPClient class >> timeout
  ^ Smalltalk at: #HTTPClientTimeout ifAbsent: [30000].

HTTPClient class >> timeout: milliseconds
  Smalltalk at: #HTTPClientTimeout put: milliseconds.
  ^ milliseconds.

HTTPClient class >> get: aUrl
  ^ self request: 'GET' url: aUrl headers: nil body: nil.

HTTPClient class >> delete: aUrl
  ^ self request: 'DELETE' url: aUrl headers: nil body: nil.

HTTPClient class >> post: aUrl body: aString
  ^ self request: 'POST' url: aUrl headers: nil body: aString.

HTTPClient class >> put: aUrl body: aString
  ^ self request: 'PUT' url: aUrl headers: nil body: aString.

HTTPClient class >> post: aUrl json: anObject
  ^ self request: 'POST' url: aUrl
      headers: (Dictionary new at: 'content-type' put: 'application/json'; yourself)
      body: (JSON stringify: anObject).

HTTPClient class >> put: aUrl json: anObject
  ^ self request: 'PUT' url: aUrl
      headers: (Dictionary new at: 'content-type' put: 'application/json'; yourself)
      body: (JSON stringify: anObject).

HTTPClient class >> request: aMethod url: aUrl headers: aDictionaryOrNil body: aStringOrNil
  ^ self request: aMethod url: aUrl headers: aDictionaryOrNil body: aStringOrNil redirects: 5.

HTTPClient class >> parseUrl: aUrl
  "#(scheme host port pathAndQuery)"
  | rest scheme i hostPort host port path |
  i := aUrl indexOf: $:.
  (i > 0 and: [(aUrl copyFrom: i to: (i + 2 min: aUrl size)) = '://'])
    ifFalse: [^ Error signal: 'not an absolute URL: ' , aUrl].
  scheme := (aUrl copyFrom: 1 to: i - 1) asLowercase.
  rest := aUrl copyFrom: i + 3 to: aUrl size.
  i := rest indexOf: $/.
  i = 0
    ifTrue: [hostPort := rest. path := '/']
    ifFalse: [hostPort := rest copyFrom: 1 to: i - 1. path := rest copyFrom: i to: rest size].
  i := hostPort indexOf: $:.
  i = 0
    ifTrue: [host := hostPort. port := scheme = 'https' ifTrue: [443] ifFalse: [80]]
    ifFalse: [host := hostPort copyFrom: 1 to: i - 1. port := (hostPort copyFrom: i + 1 to: hostPort size) asNumber].
  ^ Array with: scheme with: host with: port with: path.

HTTPClient class >> request: aMethod url: aUrl headers: aDictionaryOrNil body: aStringOrNil redirects: aCount
  | parts socket sent statusLine words status reason headers body location response |
  parts := self parseUrl: aUrl.
  socket := Socket connectTo: (parts at: 2) port: (parts at: 3) timeout: self timeout.
  ^ [
    socket timeout: self timeout.
    (parts at: 1) = 'https' ifTrue: [socket tlsHost: (parts at: 2)].
    sent := Dictionary new.
    sent at: 'host' put: (((parts at: 3) = 80 or: [(parts at: 3) = 443])
                            ifTrue: [parts at: 2]
                            ifFalse: [(parts at: 2) , ':' , (parts at: 3) printString]).
    sent at: 'user-agent' put: 'protoST/' , Smalltalk version displayString.
    sent at: 'accept' put: '*/*'.
    aStringOrNil isNil ifFalse: [sent at: 'content-type' put: 'text/plain; charset=utf-8'].
    aDictionaryOrNil isNil ifFalse: [aDictionaryOrNil keysAndValuesDo: [:k :v | sent at: k asLowercase put: v]].
    socket nextPutAll: aMethod , ' ' , (parts at: 4) , ' HTTP/1.1'; crlf.
    HTTP write: (aStringOrNil ifNil: ['']) headers: sent to: socket.
    statusLine := socket nextLine.
    statusLine isNil ifTrue: [^ NetworkError signal: 'no response from ' , aUrl].
    words := statusLine substrings: ' '.
    status := (words at: 2) asNumber.
    reason := words size > 2
      ifTrue: [statusLine copyFrom: (words at: 1) size + (words at: 2) size + 3 to: statusLine size]
      ifFalse: [''].
    headers := HTTP readHeadersFrom: socket.
    body := (aMethod = 'HEAD' or: [status = 204 or: [status = 304]])
      ifTrue: ['']
      ifFalse: [((headers includesKey: 'content-length') or: [headers includesKey: 'transfer-encoding'])
        ifTrue: [HTTP readBodyFrom: socket headers: headers]
        ifFalse: [socket upToEnd]].
    response := HTTPResponse new setStatus: status reason: reason headers: headers body: body.
    location := headers at: 'location' ifAbsent: [nil].
    ((#(301 302 303 307 308) includes: status) and: [location notNil and: [aCount > 0]])
      ifTrue: [
        (location beginsWith: '/') ifTrue: [
          location := (parts at: 1) , '://' , (sent at: 'host') , location].
        self request: (status = 303 ifTrue: ['GET'] ifFalse: [aMethod])
             url: location headers: aDictionaryOrNil
             body: (status = 303 ifTrue: [nil] ifFalse: [aStringOrNil])
             redirects: aCount - 1]
      ifFalse: [response]
  ] ensure: [socket close].

"-- Server -------------------------------------------------------------------------"

Object subclass: #HTTPConnection instanceVariableNames: 'socket handler'.

HTTPConnection >> setSocket: aSocket handler: aBlock
  socket := aSocket.
  handler := aBlock.
  ^ self.

HTTPConnection >> handle
  [self serve] ensure: [socket close].
  ^ nil.

HTTPConnection >> serve
  | line words target i path query headers body request result response |
  socket timeout: 30000.
  line := socket nextLine.
  (line isNil or: [line isEmpty]) ifTrue: [^ nil].
  words := line substrings: ' '.
  words size < 2 ifTrue: [^ self send: (HTTPResponse status: 400 body: 'Bad Request')].
  target := words at: 2.
  i := target indexOf: $?.
  i = 0
    ifTrue: [path := target. query := '']
    ifFalse: [path := target copyFrom: 1 to: i - 1. query := target copyFrom: i + 1 to: target size].
  headers := HTTP readHeadersFrom: socket.
  body := HTTP readBodyFrom: socket headers: headers.
  request := HTTPRequest new setMethod: (words at: 1) path: (HTTP percentDecode: path)
    query: (HTTP parseQuery: query) headers: headers body: body peer: socket peerName.
  result := [handler value: request]
    on: Error do: [:e | HTTPResponse status: 500 body: e messageText displayString].
  response := (result isKindOf: HTTPResponse)
    ifTrue: [result]
    ifFalse: [result isNil
      ifTrue: [HTTPResponse noContent]
      ifFalse: [result isString
        ifTrue: [HTTPResponse ok: result]
        ifFalse: [HTTPResponse json: result]]].
  ^ self send: response.

HTTPConnection >> send: aResponse
  socket nextPutAll: 'HTTP/1.1 ' , aResponse status printString , ' ' , aResponse reason; crlf.
  HTTP write: aResponse body headers: aResponse headers to: socket.
  ^ aResponse.

Object subclass: #HTTPServer instanceVariableNames: 'port handler server running loop'.

HTTPServer class >> on: aPort handler: aBlock
  ^ self new setPort: aPort handler: aBlock.

HTTPServer >> setPort: aPort handler: aBlock
  port := aPort.
  handler := aBlock.
  running := false.
  ^ self.

HTTPServer >> listen
  server isNil ifTrue: [server := ServerSocket listenOn: port].
  ^ self.

HTTPServer >> port
  "The port it listens on (the actual one when created with port 0)."
  self listen.
  ^ server port.

HTTPServer >> start
  "Serves on the calling thread until `stop`."
  self listen.
  running := true.
  ^ self serveLoop.

HTTPServer >> startInBackground
  "Serves on an actor; answers the server at once."
  self listen.
  running := true.
  loop := self asActor serveLoop.
  ^ self.

HTTPServer >> serveLoop
  [running] whileTrue: [
    | c |
    c := server acceptTimeout: 200.
    c isNil ifFalse: [(HTTPConnection new setSocket: c handler: handler) asActor handle]].
  server close.
  server := nil.
  ^ self.

HTTPServer >> isRunning
  ^ running.

HTTPServer >> stop
  running := false.
  loop isNil ifFalse: [loop wait. loop := nil].
  ^ self.

HTTPServer >> printOn: aStream
  aStream nextPutAll: 'an HTTPServer on port ' , port printString.
