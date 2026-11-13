"EXPECT: true true"
"§13 stdlib: DateTime nowUTC reads Time utcMicroseconds (the wall clock,
 microseconds since 1970-01-01T00:00Z, protoST 0.10.0) through
 fromUnixMicroseconds:zone:: it is a UTC value (zone ZoneOffset utc) whose
 year lies between 2026 and 2100. This is the one calendar test that reads
 the machine clock (the brief asks for it): a host whose clock is set
 outside 2026..2100 fails it by design. The binary without that primitive
 skips visibly; the test is verified once the 0.10.0 binary (Task 2 of the
 protoBaseST plan) is built."
| m now |
(Time respondsTo: #utcMicroseconds) ifFalse: [^ 'SKIP: needs protoST 0.10.0'].
m := Import from: 'calendar'.
now := m DateTime nowUTC.
(now year between: 2026 and: 2100) printString , ' ' , (now zone = m ZoneOffset utc) printString.
