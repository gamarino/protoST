"EXPECT: UTC UTC-03:00 UTC+05:30:07.000008 UTC-00:30 Bogota true true America/New_York true 2026-11-01 06:30:00Z[America/New_York] true true unavailable unavailable true true"
"§13 stdlib: zones. ZoneOffset names its offset as CPython's
 timezone(timedelta(...)).tzname(None) does when no name is given, keeps a
 given name, and is = by offset alone (as Python's timezone). ZoneName keeps
 an IANA key as text. A DateTime in a ZoneName is the instant form
 (utcFields:zoneName:): its fields are the UTC instant, fold 0, and
 utcOrdinalMicroseconds answers them; it is = (equal hash) to the fixed-offset
 value of the same instant. Its wallClock, and building a DateTime from a
 wall clock in a ZoneName, signal TimeZoneUnavailable (an Error)."
| m DT O Z ny inst fixed w1 w2 |
m := Import from: 'calendar'.
DT := m DateTime. O := m ZoneOffset. Z := m ZoneName.
ny := Z key: 'America/New_York'.
inst := DT utcFields: #(2026 11 1 6 30 0 0) zoneName: ny.
fixed := DT year: 2026 month: 11 day: 1 hour: 1 minute: 30 second: 0 microsecond: 0 fold: 0
  zone: (O microseconds: -18000000000).
w1 := [inst wallClock. 'answered'] on: m TimeZoneUnavailable do: [:e | 'unavailable'].
w2 := [DT year: 2026 month: 11 day: 1 hour: 1 minute: 30 second: 0 microsecond: 0 fold: 1 zone: ny.
       'built'] on: m TimeZoneUnavailable do: [:e | 'unavailable'].
{ O utc name.
  (O microseconds: -10800000000) name.
  (O microseconds: 19807000008) name.
  (O microseconds: -1800000000) name.
  (O microseconds: -18000000000 name: 'Bogota') name.
  (O microseconds: 0 name: 'X') = O utc and: [(O microseconds: 0 name: 'X') hash = O utc hash].
  (O microseconds: 0) ~= (O microseconds: 60000000).
  ny key.
  ny = (Z key: 'America/New_York') and: [ny hash = (Z key: 'America/New_York') hash and: [ny ~= (Z key: 'Europe/Paris')]].
  inst printString.
  inst utcOrdinalMicroseconds = inst localOrdinalMicroseconds and: [inst fold = 0 and: [inst zone = ny and: [inst isNaive not]]].
  inst = fixed and: [inst hash = fixed hash].
  w1.
  w2.
  (m TimeZoneUnavailable inheritsFrom: Error).
  (DT utcOrdinalMicroseconds: inst utcOrdinalMicroseconds zone: ny) = inst }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each displayString] ifFalse: [acc , ' ' , each displayString]].
