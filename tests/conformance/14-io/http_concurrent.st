"EXPECT: 16 136"
"HTTP: sixteen actors query one server at the same time."
Import from: 'http'.
Object subclass: #Caller.
Caller >> call: aUrl
  ^ (HTTPClient get: aUrl) body asNumber

server := HTTPServer on: 0 handler: [:req | (req query at: 'n') ].
server startInBackground.
base := 'http://127.0.0.1:' , server port printString , '/?n='.
futures := (1 to: 16) collect: [:i | Caller new asActor call: base , i printString].
total := futures inject: 0 into: [:sum :f | sum + f wait].
server stop.
futures size printString , ' ' , total printString.
