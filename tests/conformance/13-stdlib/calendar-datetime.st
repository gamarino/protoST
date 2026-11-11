"EXPECT: 63929908800000000 63929919600000000 true true true false refused true 2026-11-10 12:00:00-03:00 2026-11-10 12:00:00.000005 true true true refused"
"§13 stdlib: DateTime, naive and fixed-offset. localOrdinalMicroseconds of
 2026-11-10 12:00 (CPython: (dt - datetime(1,1,1)) // timedelta(microseconds=1));
 utcOrdinalMicroseconds of 12:00 at UTC-03:00 is that of 15:00 UTC, the two are =
 with equal hashes; fold does not change a naive value's equality or hash;
 a naive value is never = to an aware one and ordering them signals Error;
 utcOrdinalMicroseconds:zone: rebuilds the value (fields and zone); printString is
 CPython's str(datetime); < orders aware values by instant; utcOrdinalMicroseconds
 of a naive value signals Error."
| m DT O minus3 a b n n1 rebuilt r u |
m := Import from: 'calendar'.
DT := m DateTime. O := m ZoneOffset.
minus3 := O microseconds: -10800000000.
a := DT year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 0 microsecond: 0 fold: 0 zone: minus3.
b := DT year: 2026 month: 11 day: 10 hour: 15 minute: 0 second: 0 microsecond: 0 fold: 0 zone: O utc.
n := DT year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 0 microsecond: 0 fold: 0 zone: nil.
n1 := DT year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 0 microsecond: 0 fold: 1 zone: nil.
r := [n < b. 'ordered'] on: Error do: [:e | 'refused'].
u := [n utcOrdinalMicroseconds. 'answered'] on: Error do: [:e | 'refused'].
rebuilt := DT utcOrdinalMicroseconds: b utcOrdinalMicroseconds zone: minus3.
{ n localOrdinalMicroseconds.
  a utcOrdinalMicroseconds.
  a utcOrdinalMicroseconds = b utcOrdinalMicroseconds.
  a = b and: [a hash = b hash].
  n = n1 and: [n hash = n1 hash].
  n = a.
  r.
  rebuilt hour = 12 and: [rebuilt zone = minus3 and: [rebuilt = a and: [rebuilt isNaive not]]].
  a printString.
  (DT year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 0 microsecond: 5 fold: 0 zone: nil) printString.
  b < (DT year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 1 microsecond: 0 fold: 0 zone: minus3).
  n isNaive and: [n zone isNil and: [n1 fold = 1]].
  a wallClock == a.
  u }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each displayString] ifFalse: [acc , ' ' , each displayString]].
