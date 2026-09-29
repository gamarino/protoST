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
actor. An error in the handler answers a plain 500 and is reported on
standard error (its text is not sent to the client). A request answers
method, path, query (a Dictionary), headers (lower-case keys), body and json.

http and https (TLS with certificate and host-name verification), chunked
bodies, redirects (up to 5), `HTTPClient timeout:` (milliseconds, default
30000). One request per connection (Connection: close); no HTTP/2, no
WebSockets, no TLS server.

Redirects: a relative Location resolves against the request URL; a redirect
to another origin (scheme, host or port) drops the headers the program set,
such as Authorization; a redirect from https to http is refused.

Untrusted input is bounded: a request or header line over 8 KiB, more than
100 header lines, a malformed Content-Length and a body over the server's
`maxBodySize:` (default 64 MiB) are refused with 431, 400 or 413 before the
handler runs. A method, header name, header value or URL that contains a line
break or another control character is refused with an Error before anything
is sent: it could forge extra headers or requests."

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

HTTP class >> maxLine
  ^ 8192.

HTTP class >> maxHeaderLines
  ^ 100.

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
  "%XX sequences decoded as UTF-8 bytes; '+' is kept (see parseQuery:)."
  ^ self __percentDecode: aString.

HTTP class >> parseQuery: aString
  "In a query string '+' is a space, so it is replaced before decoding
   (an encoded plus, %2B, stays a plus)."
  | d |
  d := Dictionary new.
  (aString isNil or: [aString isEmpty]) ifTrue: [^ d].
  (aString substrings: '&') do: [:pair |
    | i p |
    p := pair collect: [:c | c = $+ ifTrue: [Character space] ifFalse: [c]].
    i := p indexOf: $=.
    i = 0
      ifTrue: [d at: (self percentDecode: p) put: '']
      ifFalse: [d at: (self percentDecode: (p copyFrom: 1 to: i - 1))
                  put: (self percentDecode: (p copyFrom: i + 1 to: p size))]].
  ^ d.

HTTP class >> check: aString as: aWhat token: isToken
  "Refuses control characters (a tab is allowed in a value) and, in a token
   (a method or header name), spaces and separators."
  aString isEmpty & isToken ifTrue: [^ Error signal: 'invalid ' , aWhat , ': empty'].
  aString do: [:c |
    | v |
    v := c value.
    ((v < 32 and: [isToken or: [v ~= 9]]) or: [v = 127
        or: [isToken and: [v = 32 or: ['()<>@,;:\"/[]?={}' includes: c]]]])
      ifTrue: [^ Error signal: 'invalid ' , aWhat , ': ' , aString printString]].
  ^ aString.

HTTP class >> checkTarget: aString
  "A request target: no spaces or control characters."
  aString do: [:c |
    (c value <= 32 or: [c value = 127])
      ifTrue: [^ Error signal: 'invalid URL: ' , aString printString]].
  ^ aString.

HTTP class >> checkHeaders: aDictionary
  aDictionary keysAndValuesDo: [:k :v |
    self check: k as: 'header name' token: true.
    self check: v displayString as: 'header value' token: false].
  ^ aDictionary.

HTTP class >> readHeadersFrom: aSocket
  "Header lines up to the blank line: a Dictionary with lower-case names.
   A line over maxLine bytes, or more than maxHeaderLines lines, raises
   LineTooLong."
  | d line count |
  d := Dictionary new.
  count := 0.
  [(line := aSocket nextLineMax: self maxLine) notNil and: [line notEmpty]] whileTrue: [
    | i |
    count := count + 1.
    count > self maxHeaderLines ifTrue: [^ LineTooLong signal: 'more than ' , self maxHeaderLines printString , ' header lines'].
    i := line indexOf: $:.
    i > 0 ifTrue: [d at: (line copyFrom: 1 to: i - 1) trimBoth asLowercase
                     put: (line copyFrom: i + 1 to: line size) trimBoth]].
  ^ d.

HTTP class >> contentLengthIn: aDictionary
  "The Content-Length, nil when absent; a value that is not a decimal
   integer raises NetworkError."
  | v |
  v := aDictionary at: 'content-length' ifAbsent: [^ nil].
  (v notEmpty and: [v size <= 18 and: [v allSatisfy: [:c | c isDigit]]])
    ifFalse: [^ NetworkError signal: 'invalid content-length: ' , v printString].
  ^ v asNumber.

HTTP class >> readBodyFrom: aSocket headers: aDictionary
  ^ self readBodyFrom: aSocket headers: aDictionary max: 0.

HTTP class >> readBodyFrom: aSocket headers: aDictionary max: aByteCount
  "The body the headers announce. aByteCount 0 means no limit; a larger body
   raises BodyTooLarge."
  | te len |
  te := aDictionary at: 'transfer-encoding' ifAbsent: [''].
  (te asLowercase includesSubstring: 'chunked')
    ifTrue: [^ self __httpReadChunked: aSocket descriptor max: aByteCount].
  len := self contentLengthIn: aDictionary.
  len isNil ifTrue: [^ ''].
  (aByteCount > 0 and: [len > aByteCount])
    ifTrue: [^ BodyTooLarge signal: 'a body of ' , len printString , ' bytes'].
  len = 0 ifTrue: [^ ''].
  ^ (aSocket nextByteCount: len) ifNil: [''].

HTTP class >> write: aBody headers: aDictionary to: aSocket
  ^ self write: aBody headers: aDictionary to: aSocket length: true.

HTTP class >> write: aBody headers: aDictionary to: aSocket length: aBoolean
  "The header lines and the body. The headers were checked (checkHeaders:)."
  aDictionary keysAndValuesDo: [:k :v |
    aSocket nextPutAll: k , ': ' , v displayString; crlf].
  aBoolean ifTrue: [aSocket nextPutAll: 'content-length: ' , (self __byteSize: aBody) printString; crlf].
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
  (path notEmpty and: [(path at: 1) = $?]) ifTrue: [path := '/' , path].
  i := hostPort indexOf: $:.
  i = 0
    ifTrue: [host := hostPort. port := scheme = 'https' ifTrue: [443] ifFalse: [80]]
    ifFalse: [host := hostPort copyFrom: 1 to: i - 1. port := (hostPort copyFrom: i + 1 to: hostPort size) asNumber].
  port isNil ifTrue: [^ Error signal: 'invalid port in URL: ' , aUrl].
  ^ Array with: scheme with: host with: port with: path.

HTTPClient class >> authorityOf: parts
  "host, or host:port when the port is not the scheme's default."
  ^ (((parts at: 1) = 'http' and: [(parts at: 3) = 80]) or: [(parts at: 1) = 'https' and: [(parts at: 3) = 443]])
      ifTrue: [parts at: 2]
      ifFalse: [(parts at: 2) , ':' , (parts at: 3) printString].

HTTPClient class >> originOf: aUrl
  | parts |
  parts := self parseUrl: aUrl.
  ^ (parts at: 1) , '://' , (parts at: 2) , ':' , (parts at: 3) printString.

HTTPClient class >> removeDotSegments: aPath
  "'/a/b/../c' -> '/a/c'; a query is kept as it is."
  | q path query out result |
  q := aPath indexOf: $?.
  q = 0
    ifTrue: [path := aPath. query := '']
    ifFalse: [path := aPath copyFrom: 1 to: q - 1. query := aPath copyFrom: q to: aPath size].
  out := OrderedCollection new.
  (path substrings: '/') do: [:seg |
    seg = '..'
      ifTrue: [out isEmpty ifFalse: [out removeLast]]
      ifFalse: [seg = '.' ifFalse: [out add: seg]]].
  result := WriteStream on: String new.
  out do: [:seg | result nextPut: $/; nextPutAll: seg].
  (out isEmpty or: [(path endsWith: '/') or: [(path endsWith: '/.') or: [path endsWith: '/..']]])
    ifTrue: [result nextPut: $/].
  ^ result contents , query.

HTTPClient class >> lastSlashIn: aPath
  aPath size to: 1 by: -1 do: [:i | (aPath at: i) = $/ ifTrue: [^ i]].
  ^ 0.

HTTPClient class >> redirectUrl: aLocation from: aUrl
  "The absolute URL a Location header names, resolved against the URL that
   answered it (RFC 3986). A redirect from https to http raises NetworkError."
  | base target dir basePath q |
  base := self parseUrl: aUrl.
  target := (aLocation includesSubstring: '://')
    ifTrue: [aLocation]
    ifFalse: [(aLocation beginsWith: '//')
      ifTrue: [(base at: 1) , ':' , aLocation]
      ifFalse: [
        basePath := base at: 4.
        q := basePath indexOf: $?.
        q > 0 ifTrue: [basePath := basePath copyFrom: 1 to: q - 1].
        (base at: 1) , '://' , (self authorityOf: base) , (self removeDotSegments:
          ((aLocation beginsWith: '/')
            ifTrue: [aLocation]
            ifFalse: [(aLocation beginsWith: '?')
              ifTrue: [basePath , aLocation]
              ifFalse: [
                dir := basePath copyFrom: 1 to: (self lastSlashIn: basePath).
                dir , aLocation]]))]].
  ((base at: 1) = 'https' and: [((self parseUrl: target) at: 1) = 'http'])
    ifTrue: [^ NetworkError signal: 'refusing a redirect from https to http: ' , target].
  ^ target.

HTTPClient class >> request: aMethod url: aUrl headers: aDictionaryOrNil body: aStringOrNil redirects: aCount
  | parts socket sent statusLine words status reason headers body location target response |
  parts := self parseUrl: aUrl.
  HTTP check: aMethod as: 'method' token: true.
  HTTP checkTarget: (parts at: 4).
  sent := Dictionary new.
  sent at: 'host' put: (self authorityOf: parts).
  sent at: 'user-agent' put: 'protoST/' , (Smalltalk version substrings: ' ') last.
  sent at: 'accept' put: '*/*'.
  aStringOrNil isNil ifFalse: [sent at: 'content-type' put: 'text/plain; charset=utf-8'].
  aDictionaryOrNil isNil ifFalse: [aDictionaryOrNil keysAndValuesDo: [:k :v | sent at: k asLowercase put: v]].
  HTTP checkHeaders: sent.
  socket := Socket connectTo: (parts at: 2) port: (parts at: 3) timeout: self timeout.
  response := [
    socket timeout: self timeout.
    (parts at: 1) = 'https' ifTrue: [socket tlsHost: (parts at: 2)].
    socket nextPutAll: aMethod , ' ' , (parts at: 4) , ' HTTP/1.1'; crlf.
    "A Content-Length only when there is a body, or the method carries one."
    HTTP write: (aStringOrNil ifNil: ['']) headers: sent to: socket
      length: (aStringOrNil notNil or: [#('POST' 'PUT' 'PATCH') includes: aMethod]).
    statusLine := socket nextLineMax: HTTP maxLine.
    statusLine isNil ifTrue: [NetworkError signal: 'no response from ' , aUrl].
    words := statusLine substrings: ' '.
    status := words size >= 2 ifTrue: [(words at: 2) asNumber] ifFalse: [nil].
    ((words at: 1 ifAbsent: ['']) beginsWith: 'HTTP/') & status isInteger
      ifFalse: [NetworkError signal: 'malformed status line from ' , aUrl , ': ' , statusLine printString].
    reason := words size > 2
      ifTrue: [statusLine copyFrom: (words at: 1) size + (words at: 2) size + 3 to: statusLine size]
      ifFalse: [''].
    headers := HTTP readHeadersFrom: socket.
    body := (aMethod = 'HEAD' or: [status = 204 or: [status = 304 or: [status < 200]]])
      ifTrue: ['']
      ifFalse: [((headers includesKey: 'content-length') or: [headers includesKey: 'transfer-encoding'])
        ifTrue: [HTTP readBodyFrom: socket headers: headers]
        ifFalse: [socket upToEnd]].
    HTTPResponse new setStatus: status reason: reason headers: headers body: body
  ] ensure: [socket close].
  location := headers at: 'location' ifAbsent: [nil].
  ((#(301 302 303 307 308) includes: status) and: [location notNil and: [aCount > 0]])
    ifFalse: [^ response].
  target := self redirectUrl: location from: aUrl.
  "Another origin does not get the program's headers (Authorization, cookies)."
  ^ self request: (status = 303 ifTrue: ['GET'] ifFalse: [aMethod])
      url: target
      headers: ((self originOf: target) = (self originOf: aUrl) ifTrue: [aDictionaryOrNil] ifFalse: [nil])
      body: (status = 303 ifTrue: [nil] ifFalse: [aStringOrNil])
      redirects: aCount - 1.

"-- Server -------------------------------------------------------------------------"

Object subclass: #HTTPConnection instanceVariableNames: 'socket handler maxBody'.

HTTPConnection >> setSocket: aSocket handler: aBlock maxBody: aByteCount
  socket := aSocket.
  handler := aBlock.
  maxBody := aByteCount.
  ^ self.

HTTPConnection >> handle
  [self serve] ensure: [socket close].
  ^ nil.

HTTPConnection >> refuse: aCode
  ^ self send: (HTTPResponse status: aCode body: (HTTPResponse reasonFor: aCode)).

HTTPConnection >> internalError: anError
  "Reported on standard error; the client gets a plain 500."
  Stdio stderr nextPutAll: 'HTTPServer: error in handler: ' , anError messageText displayString; lf; flush.
  ^ HTTPResponse status: 500 body: 'Internal Server Error'.

HTTPConnection >> serve
  | line words target i path query headers body request result response |
  socket timeout: 30000.
  line := [socket nextLineMax: HTTP maxLine] on: LineTooLong do: [:e | ^ self refuse: 414].
  (line isNil or: [line isEmpty]) ifTrue: [^ nil].
  words := line substrings: ' '.
  words size < 2 ifTrue: [^ self refuse: 400].
  target := words at: 2.
  i := target indexOf: $?.
  i = 0
    ifTrue: [path := target. query := '']
    ifFalse: [path := target copyFrom: 1 to: i - 1. query := target copyFrom: i + 1 to: target size].
  headers := [HTTP readHeadersFrom: socket] on: LineTooLong do: [:e | ^ self refuse: 431].
  body := [[HTTP readBodyFrom: socket headers: headers max: maxBody]
      on: BodyTooLarge do: [:e | ^ self refuse: 413]]
    on: NetworkError do: [:e | ^ self refuse: 400].
  request := HTTPRequest new setMethod: (words at: 1) path: (HTTP percentDecode: path)
    query: (HTTP parseQuery: query) headers: headers body: body peer: socket peerName.
  result := [handler value: request] on: Error do: [:e | self internalError: e].
  response := (result isKindOf: HTTPResponse)
    ifTrue: [result]
    ifFalse: [result isNil
      ifTrue: [HTTPResponse noContent]
      ifFalse: [result isString
        ifTrue: [HTTPResponse ok: result]
        ifFalse: [HTTPResponse json: result]]].
  response := [HTTP checkHeaders: response headers. response]
    on: Error do: [:e | self internalError: e].
  ^ self send: response.

HTTPConnection >> send: aResponse
  socket nextPutAll: 'HTTP/1.1 ' , aResponse status printString , ' ' , aResponse reason; crlf.
  HTTP write: aResponse body headers: aResponse headers to: socket.
  ^ aResponse.

Object subclass: #HTTPServer instanceVariableNames: 'port handler server running loop maxBody'.

HTTPServer class >> on: aPort handler: aBlock
  ^ self new setPort: aPort handler: aBlock.

HTTPServer >> setPort: aPort handler: aBlock
  port := aPort.
  handler := aBlock.
  running := false.
  maxBody := 67108864.
  ^ self.

HTTPServer >> maxBodySize
  ^ maxBody.

HTTPServer >> maxBodySize: aByteCount
  "Requests with a larger body are refused with 413 (default 64 MiB)."
  maxBody := aByteCount.
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
  [[running] whileTrue: [
    | c |
    c := server acceptTimeout: 200.
    c isNil ifFalse: [(HTTPConnection new setSocket: c handler: handler maxBody: maxBody) asActor handle]]]
    ensure: [
      running := false.
      server isNil ifFalse: [server close. server := nil]].
  ^ self.

HTTPServer >> isRunning
  ^ running.

HTTPServer >> stop
  running := false.
  loop isNil ifFalse: [loop wait. loop := nil].
  ^ self.

HTTPServer >> printOn: aStream
  aStream nextPutAll: 'an HTTPServer on port ' ,
    (server isNil ifTrue: [port] ifFalse: [server port]) printString.
