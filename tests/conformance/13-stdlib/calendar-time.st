"EXPECT: 1 2 3 40 0 3723000040 01:02:03.000040+05:30 12:00:00 true true false true true refused"
"§13 stdlib: TimeOfDay, a wall clock with an optional zone. Fields,
 microsecondsSinceMidnight and printString (CPython's str(time), the zone
 named in brackets when it is an IANA key). Comparison as Python's time:
 12:00 at UTC-03:00 = 15:00 UTC (and equal hashes); a naive and a
 fixed-offset time are never =; a time in an IANA zone has no offset (no
 date), so it compares and hashes as the naive time of its wall clock;
 fold is ignored by =; ordering a naive time against a fixed-offset one
 signals Error."
| m T O Z t a b r |
m := Import from: 'calendar'.
T := m TimeOfDay. O := m ZoneOffset. Z := m ZoneName.
t := T hour: 1 minute: 2 second: 3 microsecond: 40 fold: 0
  zone: (O microseconds: 19800000000).
a := T hour: 12 minute: 0 second: 0 microsecond: 0 fold: 0 zone: (O microseconds: -10800000000).
b := T hour: 15 minute: 0 second: 0 microsecond: 0 fold: 0 zone: O utc.
r := [(T hour: 12 minute: 0 second: 0) < a. 'ordered'] on: Error do: [:e | 'refused'].
{ t hour. t minute. t second. t microsecond. t fold.
  t microsecondsSinceMidnight.
  t printString.
  (T hour: 12 minute: 0 second: 0) printString.
  a = b and: [a hash = b hash].
  (T hour: 12 minute: 0 second: 0) = (T hour: 12 minute: 0 second: 0 microsecond: 0 fold: 1 zone: nil).
  (T hour: 12 minute: 0 second: 0) = b.
  ((T hour: 12 minute: 0 second: 0 microsecond: 0 fold: 0 zone: (Z key: 'Europe/Paris'))
     = (T hour: 12 minute: 0 second: 0)) and: [
   (T hour: 12 minute: 0 second: 0 microsecond: 0 fold: 0 zone: (Z key: 'Europe/Paris')) hash
     = (T hour: 12 minute: 0 second: 0) hash].
  (T hour: 11 minute: 59 second: 59) < (T hour: 12 minute: 0 second: 0).
  r }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each displayString] ifFalse: [acc , ' ' , each displayString]].
