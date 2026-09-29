"-- net.st: TCP and UDP sockets --

USAGE
  Import from: 'net'.
  s := Socket connectTo: 'example.com' port: 80.
  s nextPutAll: 'GET / HTTP/1.0'; crlf; crlf; flush.
  s nextLine.                              => 'HTTP/1.0 200 OK'
  s close.

  server := ServerSocket listenOn: 8080.   (port 0: any free port; `port` answers it)
  c := server accept.                      a Socket
  c nextLine. c nextPutAll: 'hi'; lf; flush; close.

  u := UDPSocket bindTo: 9999.
  u send: 'ping' to: '127.0.0.1' port: 9998.
  u receiveTimeout: 1000.                  => #('pong' '127.0.0.1' 9998) or nil

A Socket is an IOStream (the kernel's stream over a descriptor): nextLine,
upToEnd, next:, nextBytes:, atEnd, linesDo:, nextPutAll:, nextPutBytes:,
print:, <<, cr, lf, crlf, flush, timeout:, close. `tlsHost:` upgrades a
connected socket to TLS, verifying the certificate and the host name.

Blocking calls leave the collector's quorum while they wait. Serve several
clients with one actor per connection: an actor that is blocked in a read
does not handle other messages.

Errors: ConnectionRefused, ConnectionTimedOut, NameLookupFailure and
NetworkError (all Errors, declared in the kernel)."

IOStream subclass: #Socket instanceVariableNames: 'host port'.

Socket class >> connectTo: aHost port: aPort
  ^ self connectTo: aHost port: aPort timeout: 10000.

Socket class >> connectTo: aHost port: aPort timeout: milliseconds
  ^ (self onDescriptor: (self __tcpConnect: aHost port: aPort timeout: milliseconds))
      setHost: aHost port: aPort.

Socket class >> connectToTLS: aHost port: aPort
  ^ (self connectTo: aHost port: aPort) tlsHost: aHost.

Socket >> setHost: aHost port: aPort
  host := aHost.
  port := aPort.
  ^ self.

Socket >> tlsHost: aHostName
  "Starts TLS on this connection, verifying the server's certificate and that
   it names aHostName."
  self __tlsConnect: self descriptor host: aHostName verify: true.
  ^ self.

Socket >> tlsHostUnverified: aHostName
  "TLS without certificate verification (self-signed test servers only)."
  self __tlsConnect: self descriptor host: aHostName verify: false.
  ^ self.

Socket >> peerAddress
  ^ self __sockName: self descriptor peer: true.

Socket >> peerName
  | a |
  a := self peerAddress.
  ^ (a at: 1) , ':' , (a at: 2) printString.

Socket >> localPort
  ^ (self __sockName: self descriptor peer: false) at: 2.

Socket >> printOn: aStream
  aStream nextPutAll: 'a Socket'.
  host isNil ifFalse: [aStream nextPutAll: ' to ' , host , ':' , port printString].

Object subclass: #ServerSocket instanceVariableNames: 'fd'.

ServerSocket class >> listenOn: aPort
  ^ self listenOn: aPort host: ''.

ServerSocket class >> listenOn: aPort host: aHost
  "aHost '' listens on every interface; '127.0.0.1' only locally."
  ^ self basicNew setDescriptor: (self __tcpListen: aHost port: aPort backlog: 128).

ServerSocket >> setDescriptor: anInteger
  fd := anInteger.
  ^ self.

ServerSocket >> port
  ^ (self __sockName: fd peer: false) at: 2.

ServerSocket >> accept
  ^ Socket onDescriptor: (self __tcpAccept: fd timeout: nil).

ServerSocket >> acceptTimeout: milliseconds
  "A Socket, or nil when no client connected in time."
  | c |
  c := self __tcpAccept: fd timeout: milliseconds.
  ^ c isNil ifTrue: [nil] ifFalse: [Socket onDescriptor: c].

ServerSocket >> close
  fd isNil ifFalse: [self __fdClose: fd. fd := nil].
  ^ self.

ServerSocket >> printOn: aStream
  aStream nextPutAll: 'a ServerSocket'.

Object subclass: #UDPSocket instanceVariableNames: 'fd'.

UDPSocket class >> bindTo: aPort
  ^ self bindTo: aPort host: ''.

UDPSocket class >> bindTo: aPort host: aHost
  ^ self basicNew setDescriptor: (self __udpBind: aHost port: aPort).

UDPSocket >> setDescriptor: anInteger
  fd := anInteger.
  ^ self.

UDPSocket >> port
  ^ (self __sockName: fd peer: false) at: 2.

UDPSocket >> send: aStringOrBytes to: aHost port: aPort
  self __udpSend: fd to: aHost port: aPort data: aStringOrBytes.
  ^ aStringOrBytes.

UDPSocket >> receive
  "Waits for a datagram: #(data host port)."
  ^ self __udpReceive: fd timeout: nil binary: false.

UDPSocket >> receiveTimeout: milliseconds
  "#(data host port), or nil when nothing arrived in time."
  ^ self __udpReceive: fd timeout: milliseconds binary: false.

UDPSocket >> receiveBytesTimeout: milliseconds
  ^ self __udpReceive: fd timeout: milliseconds binary: true.

UDPSocket >> close
  fd isNil ifFalse: [self __fdClose: fd. fd := nil].
  ^ self.

UDPSocket >> printOn: aStream
  aStream nextPutAll: 'a UDPSocket'.
