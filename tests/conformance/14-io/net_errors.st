"EXPECT: refused lookup"
"Network errors are classed: refused connection, failed name lookup."
Import from: 'net'.
server := ServerSocket listenOn: 0.
port := server port.
server close.
a := [Socket connectTo: '127.0.0.1' port: port. 'connected'] on: ConnectionRefused do: [:e | 'refused'].
b := [Socket connectTo: 'no-such-host.invalid' port: 80. 'connected'] on: NameLookupFailure do: [:e | 'lookup'].
a , ' ' , b.
