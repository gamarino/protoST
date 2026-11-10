"EXPECT: #[1 0 2] true #[1 0 2] 127.0.0.1"
"UDP: a ByteArray holding a NUL travels intact; receiveBytesTimeout: answers a ByteArray."
Import from: 'net'.
rx := UDPSocket bindTo: 0.
tx := UDPSocket bindTo: 0.
tx send: #[1 0 2] to: '127.0.0.1' port: rx port.
msg := rx receiveBytesTimeout: 2000.
rx close. tx close.
(msg at: 1) printString, ' ', ((msg at: 1) class == ByteArray) printString, ' ', (msg at: 1) printString, ' ', (msg at: 2).
