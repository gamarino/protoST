"EXPECT: false true protoST/0.6.5"
"A GET without a body sends no Content-Length; a POST does; the user agent
 names the version."
Import from: 'http'.
Object subclass: #Recorder.
Recorder >> headersOf: srv
  | c h |
  c := srv accept.
  c nextLine.
  h := HTTP readHeadersFrom: c.
  "Read the body before replying: closing a socket with unread input makes
   Windows reset the connection, and the client would see that reset."
  (h includesKey: 'content-length') ifTrue: [
    c nextByteCount: (h at: 'content-length') asNumber].
  c nextPutAll: 'HTTP/1.1 204 No Content'; crlf; nextPutAll: 'connection: close'; crlf; crlf; flush; close.
  ^ h.
srv := ServerSocket listenOn: 0 host: '127.0.0.1'.
url := 'http://127.0.0.1:' , srv port printString , '/'.
f := Recorder new asActor headersOf: srv.
HTTPClient get: url.
getHeaders := f wait.
f := Recorder new asActor headersOf: srv.
HTTPClient post: url body: 'x'.
postHeaders := f wait.
srv close.
(getHeaders includesKey: 'content-length') printString , ' ' ,
  (postHeaders includesKey: 'content-length') printString , ' ' , (getHeaders at: 'user-agent').
