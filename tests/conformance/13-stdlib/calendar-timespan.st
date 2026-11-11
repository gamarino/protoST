"EXPECT: -1 86399 0 | 2 3599 999999 | -1 86399 999999 | -3 7 500000 | -1 day, 23:59:59 | 2 days, 1:01:01.000005 | 0:00:00 | 86399999999999999999 -86399999913600000000 | true true true refused"
"§13 stdlib: TimeSpan, normalised as CPython's timedelta (days, seconds,
 microseconds as timedelta(days=d, seconds=s, microseconds=us) prints them):
 (0, -1, 0), (1, 90000, -1), microseconds: -1 and (-3, 5, 2500000).
 printString is str(timedelta). totalMicroseconds of timedelta.max and
 timedelta.min. Equal spans are = with equal hashes, < orders by duration,
 days beyond 999999999 signal Error."
| m S f ok |
m := Import from: 'calendar'.
S := m TimeSpan.
f := [:s | s days printString , ' ' , s seconds printString , ' ' , s microseconds printString].
ok := [S days: 1000000000 seconds: 0 microseconds: 0. 'built'] on: Error do: [:e | 'refused'].
{ f value: (S days: 0 seconds: -1 microseconds: 0).
  f value: (S days: 1 seconds: 90000 microseconds: -1).
  f value: (S microseconds: -1).
  f value: (S days: -3 seconds: 5 microseconds: 2500000).
  (S days: 0 seconds: -1 microseconds: 0) printString.
  (S days: 2 seconds: 3661 microseconds: 5) printString.
  (S microseconds: 0) printString.
  (S days: 999999999 seconds: 86399 microseconds: 999999) totalMicroseconds printString , ' '
    , (S days: -999999999 seconds: 0 microseconds: 0) totalMicroseconds printString.
  ((S days: 1 seconds: 0 microseconds: 0) = (S days: 0 seconds: 86400 microseconds: 0)
    and: [(S days: 1 seconds: 0 microseconds: 0) hash = (S days: 0 seconds: 86400 microseconds: 0) hash])
    printString , ' ' , ((S microseconds: -1) < (S microseconds: 0)) printString
    , ' ' , ((S microseconds: 1) = 1) not printString , ' ' , ok }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each] ifFalse: [acc , ' | ' , each]].
