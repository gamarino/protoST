"EXPECT: true"
"Time utcMicroseconds: microseconds since 1970-01-01T00:00Z from the wall
 clock (protoIO clock::utcMicroseconds). It is after 2026-01-01 and, read in
 milliseconds, lies between two readings of the wall clock taken around it
 (nil __currentMillis, the same system clock). No elapsed time is asserted."
| t0 u t1 |
t0 := nil __currentMillis.
u := Time utcMicroseconds.
t1 := nil __currentMillis.
(u > 1767225600000000) & (u // 1000 between: t0 and: t1).
