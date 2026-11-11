"EXPECT: 20 refused"
"§13 stdlib: more calendar refusals, each an Error: 29 February 2026 (not a
 leap year) and 1900 (not a leap year), ordinal 0 and 3652060, hour 24,
 minute 60, second 60, microsecond 10^6, fold 2, a zone that is neither nil,
 a ZoneOffset nor a ZoneName, an offset of -86400 * 10^6, a non-String
 offset name, an empty ZoneName key, utcFields: of 6 fields, utcFields: with
 a ZoneOffset, utcMicroseconds:zone: with nil, a TimeSpan of -10^9 days, a
 non-Integer TimeSpan field, ordering a TimeSpan against an Integer, and a
 second call of a private initialiser on a live value (immutability)."
| m n DT T |
m := Import from: 'calendar'.
DT := m DateTime. T := m TimeOfDay.
n := 0.
{ [m CalendarDate year: 2026 month: 2 day: 29].
  [m CalendarDate year: 1900 month: 2 day: 29].
  [m CalendarDate fromOrdinal: 0].
  [m CalendarDate fromOrdinal: 3652060].
  [T hour: 24 minute: 0 second: 0].
  [T hour: 0 minute: 60 second: 0].
  [T hour: 0 minute: 0 second: 60].
  [T hour: 0 minute: 0 second: 0 microsecond: 1000000 fold: 0 zone: nil].
  [T hour: 0 minute: 0 second: 0 microsecond: 0 fold: 2 zone: nil].
  [T hour: 0 minute: 0 second: 0 microsecond: 0 fold: 0 zone: 'UTC'].
  [m ZoneOffset microseconds: -86400000000 name: 'X'].
  [m ZoneOffset microseconds: 0 name: 42].
  [m ZoneName key: ''].
  [DT utcFields: #(2026 11 1 6 30 0) zoneName: (m ZoneName key: 'UTC')].
  [DT utcFields: #(2026 11 1 6 30 0 0) zoneName: m ZoneOffset utc].
  [DT utcMicroseconds: 0 zone: nil].
  [m TimeSpan days: -1000000000 seconds: 0 microseconds: 0].
  [m TimeSpan days: 0 seconds: 1.5 microseconds: 0].
  [(m TimeSpan microseconds: 1) < 2].
  [(m CalendarDate year: 2026 month: 11 day: 10) _setYear: 2000 month: 1 day: 1] }
  do: [:blk | blk on: Error do: [:e | n := n + 1]].
n printString , ' refused'.
