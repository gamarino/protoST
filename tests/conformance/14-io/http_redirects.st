"EXPECT: /dir/next none refused http://h/a/x http://h/a/b/x?q=1 https://g/y"
"Redirects: a relative Location resolves against the request URL, headers the
 program set are not sent to another host, and https never follows to http."
Import from: 'http'.
b := HTTPServer on: 0 handler: [:req | (req headerAt: 'authorization') ifNil: ['none']].
b startInBackground.
bUrl := 'http://127.0.0.1:' , b port printString , '/'.
a := HTTPServer on: 0 handler: [:req |
  req path = '/dir/start'
    ifTrue: [(HTTPResponse status: 302 body: '') headerAt: 'location' put: 'next'; yourself]
    ifFalse: [req path = '/away'
      ifTrue: [(HTTPResponse status: 302 body: '') headerAt: 'location' put: bUrl; yourself]
      ifFalse: [req path]]].
a startInBackground.
aBase := 'http://127.0.0.1:' , a port printString.
r1 := HTTPClient get: aBase , '/dir/start'.
r2 := HTTPClient request: 'GET' url: aBase , '/away'
  headers: (Dictionary new at: 'Authorization' put: 'secret'; yourself) body: nil.
a stop.
b stop.
down := [HTTPClient redirectUrl: 'http://h/x' from: 'https://h/a'] on: NetworkError do: [:e | 'refused'].
r1 body , ' ' , r2 body , ' ' , down , ' ' ,
  (HTTPClient redirectUrl: '../x' from: 'http://h/a/b/c') , ' ' ,
  (HTTPClient redirectUrl: 'x?q=1' from: 'http://h/a/b/c') , ' ' ,
  (HTTPClient redirectUrl: '//g/y' from: 'https://h/a').
