"EXPECT-ERROR: MessagePack cannot encode an instance of Point"
"The same inside a container."
MessagePack encode: (Array with: 1 with: (Array with: 3 @ 4)).
