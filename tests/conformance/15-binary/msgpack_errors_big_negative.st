"EXPECT-ERROR: outside"
"-2^63 - 1 is below the int64 range."
MessagePack encode: ((2 raisedTo: 63) negated - 1).
