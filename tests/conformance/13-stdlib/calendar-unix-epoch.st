"EXPECT: true 0 1794322800000000 2025-11-10 18:40:00.123456+00:00 0001-01-01 00:00:00+00:00 1969-12-31 21:00:00-03:00 true refused"
"§13 stdlib: the two epochs of DateTime. The ...OrdinalMicroseconds selectors
 count from 0001-01-01T00:00, the ...UnixMicroseconds ones from
 1970-01-01T00:00Z; DateTime unixEpochOrdinalMicroseconds is the Unix epoch
 on the ordinal scale. Values as CPython prints them: 1970-01-01 UTC is 0,
 2026-11-10 12:00-03:00 is 1794322800000000, fromtimestamp(1762800000.123456,
 utc), the first instant of year 1 and the Unix epoch at UTC-03:00. The pair
 round-trips; a naive value has no Unix instant."
| m DT O utc minus3 t |
m := Import from: 'calendar'.
DT := m DateTime. O := m ZoneOffset.
utc := O utc. minus3 := O microseconds: -10800000000.
t := DT year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 0 microsecond: 0 fold: 0 zone: minus3.
{ DT unixEpochOrdinalMicroseconds
    = (DT year: 1970 month: 1 day: 1 hour: 0 minute: 0 second: 0 microsecond: 0 fold: 0 zone: utc)
        utcOrdinalMicroseconds.
  (DT year: 1970 month: 1 day: 1 hour: 0 minute: 0 second: 0 microsecond: 0 fold: 0 zone: utc)
    unixMicroseconds.
  t unixMicroseconds.
  (DT fromUnixMicroseconds: 1762800000123456 zone: utc) printString.
  (DT fromUnixMicroseconds: -62135596800000000 zone: utc) printString.
  (DT fromUnixMicroseconds: 0 zone: minus3) printString.
  (DT fromUnixMicroseconds: t unixMicroseconds zone: minus3) = t.
  [(DT year: 1970 month: 1 day: 1 hour: 0 minute: 0 second: 0 microsecond: 0 fold: 0 zone: nil)
     unixMicroseconds. 'answered'] on: Error do: [:e | 'refused'] }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each displayString] ifFalse: [acc , ' ' , each displayString]].
