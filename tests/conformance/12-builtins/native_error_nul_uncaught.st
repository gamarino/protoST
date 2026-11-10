"EXPECT-ERROR: ' at position 1 is not a pair of hexadecimal digits"
"The uncaught-error report prints a native error's text whole: the text after
 the NUL in the offending pair '0', NUL is on standard error too."
| nul |
nul := String with: (Character value: 0).
ByteArray fromHex: '0', nul, 'zz'
