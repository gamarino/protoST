"EXPECT: true true"
"§13 stdlib: DateTime nowUTC reads Time utcMicroseconds (the wall clock,
 microseconds since 1970-01-01T00:00Z, protoST 0.9.1): it is a UTC value
 (zone ZoneOffset utc) whose year lies between 2026 and 2100. The binary
 without that primitive skips visibly."
| m now |
(Time respondsTo: #utcMicroseconds) ifFalse: [^ 'SKIP: needs protoST 0.9.1'].
m := Import from: 'calendar'.
now := m DateTime nowUTC.
(now year between: 2026 and: 2100) printString , ' ' , (now zone = m ZoneOffset utc) printString.
