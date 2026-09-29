"EXPECT: ConnectionTimedOut"
"A TLS handshake with a server that never answers ends at the client's timeout
 instead of blocking forever."
Import from: 'http'.
srv := ServerSocket listenOn: 0 host: '127.0.0.1'.
HTTPClient timeout: 500.
r := [HTTPClient get: 'https://127.0.0.1:' , srv port printString , '/'. 'answered']
  on: Error do: [:e | e class name].
srv close.
r displayString.
