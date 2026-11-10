"EXPECT-ERROR: outside"
"An Integer beyond 2^64 - 1 cannot be a MessagePack integer: it signals Error and never wraps (protoBaseST encodes big integers itself)."
MessagePack encode: (2 raisedTo: 64).
