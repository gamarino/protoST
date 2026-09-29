"EXPECT: echo: hello"
"TCP: a server on an actor answers a client on the main thread."
Import from: 'net'.
Object subclass: #EchoServer.
EchoServer >> serveOnce: aServer
  | s line |
  s := aServer accept.
  line := s nextLine.
  s nextPutAll: 'echo: ' , line; lf; flush; close.
  ^ line

server := ServerSocket listenOn: 0.
done := EchoServer new asActor serveOnce: server.
client := Socket connectTo: '127.0.0.1' port: server port.
client nextPutAll: 'hello'; lf; flush.
answer := client nextLine.
client close.
done wait.
server close.
answer.
