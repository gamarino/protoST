"EXPECT: ByteArray fromHex: '0<NUL>' at position 1 is not a pair of hexadecimal digits"
"A native error whose text echoes user data keeps every byte of it: the
 offending pair '0', NUL reaches messageText whole, with the text after the
 NUL. (Before the NativeError type, the text ended at the NUL.)"
| nul text |
nul := String with: (Character value: 0).
text := [ByteArray fromHex: '0', nul, 'zz'] on: Error do: [:e | e messageText].
text copyReplaceAll: nul with: '<NUL>'
