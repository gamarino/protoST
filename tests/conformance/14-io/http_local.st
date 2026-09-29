"EXPECT: 200 hello ana 42 xyz 404"
"HTTP: a local server on actors, queried by the client."
Import from: 'http'.
server := HTTPServer on: 0 handler: [:req |
  req path = '/json'
    ifTrue: [HTTPResponse json: (Dictionary new at: 'n' put: 42; yourself)]
    ifFalse: [req path = '/echo'
      ifTrue: [req body]
      ifFalse: [req path = '/greet'
        ifTrue: ['hello ' , (req query at: 'name' ifAbsent: ['?'])]
        ifFalse: [HTTPResponse notFound]]]].
server startInBackground.
base := 'http://127.0.0.1:' , server port printString.
r1 := HTTPClient get: base , '/greet?name=ana'.
r2 := HTTPClient get: base , '/json'.
r3 := HTTPClient post: base , '/echo' body: 'xyz'.
r4 := HTTPClient get: base , '/missing'.
server stop.
r1 status printString , ' ' , r1 body , ' ' , (r2 json at: 'n') printString , ' ' , r3 body , ' ' , r4 status printString.
