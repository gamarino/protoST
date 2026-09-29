"EXPECT: ping 127.0.0.1"
"UDP: send a datagram to a bound socket and receive it."
Import from: 'net'.
rx := UDPSocket bindTo: 0.
tx := UDPSocket bindTo: 0.
tx send: 'ping' to: '127.0.0.1' port: rx port.
msg := rx receiveTimeout: 2000.
rx close. tx close.
(msg at: 1) , ' ' , (msg at: 2).
