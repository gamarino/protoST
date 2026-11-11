"-- calendar.st: calendar values with fixed offsets --

Exact calendar values: a date, a time of day, a date and time, a time span,
and the two kinds of zone a value can carry. They count microseconds, follow
the proleptic Gregorian calendar over years 1 to 9999, and keep the model of
Python's datetime module as the ProtoBase format spec (format 4) stores it:
@date, @time, @dt and @td.

These are protoST's own classes (STATUS D50): the kernel's Date, Time and
Duration keep their meaning, and importing this module replaces none of them.
There is no IANA time-zone database. A ZoneName keeps its key as text; a
DateTime in a named zone exists only in the instant form (its fields are its
UTC instant), and asking for its wall clock, or building one from a wall
clock, signals TimeZoneUnavailable.

USAGE
  m := Import from: 'calendar'.     (binds CalendarDate, TimeOfDay, DateTime,
                                     TimeSpan, ZoneOffset, ZoneName and
                                     TimeZoneUnavailable as globals too)
  d := CalendarDate year: 2026 month: 11 day: 10.
  d ordinal.                                  => 739930
  d printString.                              => '2026-11-10'
  t := DateTime year: 2026 month: 11 day: 10 hour: 12 minute: 0 second: 0
         microsecond: 0 fold: 0 zone: (ZoneOffset microseconds: -10800000000).
  t printString.                              => '2026-11-10 12:00:00-03:00'
  t utcOrdinalMicroseconds.                   => 63929919600000000
  t unixMicroseconds.                         => 1794322800000000
  (TimeSpan days: 0 seconds: -1 microseconds: 0) days.   => -1

MICROSECOND COUNTS: TWO EPOCHS, TWO NAMES
  The ...OrdinalMicroseconds selectors count from 0001-01-01T00:00, the
  ordinal epoch of CalendarDate and the epoch of ProtoBase's format:
  localOrdinalMicroseconds, utcOrdinalMicroseconds and
  DateTime utcOrdinalMicroseconds:zone:. The ...UnixMicroseconds selectors
  count from 1970-01-01T00:00Z, the POSIX epoch of Time utcMicroseconds
  (protoST 0.9.1): unixMicroseconds and DateTime fromUnixMicroseconds:zone:.
  DateTime unixEpochOrdinalMicroseconds (62135596800000000) is the Unix epoch
  on the ordinal scale, the one constant between the two; no caller needs to
  add it by hand.

COMPARISON (as Python compares the values)
  CalendarDate and TimeSpan order by day and by duration.
  TimeOfDay: a naive time and a time in a ZoneName (which has no offset, a
  time having no date) compare by wall clock; a fixed-offset time by wall
  clock minus offset. DateTime: naive values compare by their fields; aware
  values (fixed offset or instant form) by their UTC instant, so two aware
  values of one instant in different zones are =. fold never takes part.
  A naive value is never = to an aware one, and ordering the two signals
  Error, as does ordering against a value of another class. hash agrees
  with = everywhere. ZoneOffset is = by offset alone (the name is not
  compared, as Python's timezone); ZoneName by key. Zones have no order.

All values are immutable: each class has one private initialiser, used by
its class-side constructors, which refuses a second call."

Error subclass: #TimeZoneUnavailable.

"The shared protocol of the calendar values: the comparisons derived from
 = and _orderWith: (which each class defines), and the calendar arithmetic
 its class side offers to the others."
Object subclass: #_CalendarValue instanceVariableNames: ''.

_CalendarValue >> ~= anObject
  ^ (self = anObject) not.

_CalendarValue >> < anObject
  ^ (self _orderWith: anObject) < 0.

_CalendarValue >> <= anObject
  ^ (self _orderWith: anObject) <= 0.

_CalendarValue >> > anObject
  ^ (self _orderWith: anObject) > 0.

_CalendarValue >> >= anObject
  ^ (self _orderWith: anObject) >= 0.

_CalendarValue >> max: anObject
  ^ (self _orderWith: anObject) < 0 ifTrue: [anObject] ifFalse: [self].

_CalendarValue >> min: anObject
  ^ (self _orderWith: anObject) > 0 ifTrue: [anObject] ifFalse: [self].

_CalendarValue >> _orderWith: anObject
  ^ Error signal: self class name , ' has no order'.

"Signals Error when anObject cannot be ordered against the receiver."
_CalendarValue >> _cannotOrder: anObject
  ^ Error signal: self class name , ': cannot order ' , self printString , ' and '
    , anObject printString.

"-1, 0 or 1."
_CalendarValue >> _sign: anInteger
  ^ anInteger sign.

"The class side (Foo class >> ...) holds the constructors and the helpers
 they share; an instance reaches them through its class.

 Signals Error unless aValue is an Integer in low..high."
_CalendarValue class >> _check: aValue from: low to: high named: aName
  (aValue isInteger and: [aValue between: low and: high]) ifFalse: [
    ^ Error signal: self name , ': ' , aName , ' must be an Integer in ' , low printString
      , '..' , high printString , ', not ' , aValue printString ].
  ^ aValue.

_CalendarValue class >> _isLeap: aYear
  ^ aYear \\ 4 = 0 and: [aYear \\ 100 ~= 0 or: [aYear \\ 400 = 0]].

_CalendarValue class >> _daysInMonth: aMonth year: aYear
  (aMonth = 2 and: [self _isLeap: aYear]) ifTrue: [^ 29].
  ^ #(31 28 31 30 31 30 31 31 30 31 30 31) at: aMonth.

_CalendarValue class >> _daysBeforeMonth: aMonth year: aYear
  ^ (#(0 31 59 90 120 151 181 212 243 273 304 334) at: aMonth)
    + ((aMonth > 2 and: [self _isLeap: aYear]) ifTrue: [1] ifFalse: [0]).

"Signals Error unless year, month and day name a day of years 1..9999."
_CalendarValue class >> _checkYear: y month: m day: d
  self _check: y from: 1 to: 9999 named: 'year'.
  self _check: m from: 1 to: 12 named: 'month'.
  self _check: d from: 1 to: (self _daysInMonth: m year: y) named: 'day'.

"The day number of a valid date: 0001-01-01 is 1 (Python's toordinal)."
_CalendarValue class >> _ordinalYear: y month: m day: d
  | y1 |
  y1 := y - 1.
  ^ (y1 * 365) + (y1 // 4) - (y1 // 100) + (y1 // 400)
    + (self _daysBeforeMonth: m year: y) + d.

"Signals Error unless the time fields are in range and fold is 0 or 1."
_CalendarValue class >> _checkHour: h minute: mi second: s microsecond: us fold: f
  self _check: h from: 0 to: 23 named: 'hour'.
  self _check: mi from: 0 to: 59 named: 'minute'.
  self _check: s from: 0 to: 59 named: 'second'.
  self _check: us from: 0 to: 999999 named: 'microsecond'.
  self _check: f from: 0 to: 1 named: 'fold'.

"Writes HH:MM:SS, then .ffffff when the microsecond is not 0."
_CalendarValue class >> _printHour: h minute: mi second: s microsecond: us on: aStream
  aStream
    nextPutAll: (h printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (mi printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (s printPaddedWith: $0 to: 2).
  us = 0 ifFalse: [aStream nextPut: $.; nextPutAll: (us printPaddedWith: $0 to: 6)].

"=== ZoneOffset =============================================================="

"A fixed offset east of UTC, in microseconds (|offset| < 24 h), with a name:
 Python's datetime.timezone. = compares the offset alone."
_CalendarValue subclass: #ZoneOffset instanceVariableNames: 'microseconds name'.

ZoneOffset class >> microseconds: anInteger name: aString
  self _check: anInteger from: -86399999999 to: 86399999999 named: 'the offset in microseconds'.
  aString isString ifFalse: [
    ^ Error signal: 'ZoneOffset: the name must be a String, not ' , aString printString ].
  ^ self basicNew _setMicroseconds: anInteger name: aString.

"A fixed offset named as Python names an unnamed timezone: UTC for 0, else
 UTC followed by the offset (UTC-03:00, UTC+05:30:07.000008)."
ZoneOffset class >> microseconds: anInteger
  self _check: anInteger from: -86399999999 to: 86399999999 named: 'the offset in microseconds'.
  anInteger = 0 ifTrue: [^ self microseconds: 0 name: 'UTC'].
  ^ self microseconds: anInteger name: 'UTC' , (self _isoOffset: anInteger).

ZoneOffset class >> utc
  ^ self microseconds: 0 name: 'UTC'.

"+HH:MM, then :SS and .ffffff only when not zero (Python's isoformat of a
 utcoffset)."
ZoneOffset class >> _isoOffset: anInteger
  | magnitude rest |
  magnitude := anInteger abs.
  ^ String streamContents: [:w |
    w nextPut: (anInteger < 0 ifTrue: [$-] ifFalse: [$+]).
    w nextPutAll: (magnitude // 3600000000 printPaddedWith: $0 to: 2); nextPut: $:;
      nextPutAll: (magnitude // 60000000 \\ 60 printPaddedWith: $0 to: 2).
    rest := magnitude \\ 60000000.
    rest = 0 ifFalse: [
      w nextPut: $:; nextPutAll: (rest // 1000000 printPaddedWith: $0 to: 2).
      rest \\ 1000000 = 0 ifFalse: [
        w nextPut: $.; nextPutAll: (rest \\ 1000000 printPaddedWith: $0 to: 6)]]].

ZoneOffset >> _setMicroseconds: anInteger name: aString
  microseconds isNil ifFalse: [^ Error signal: 'ZoneOffset is immutable: ' , self printString].
  microseconds := anInteger.
  name := aString.
  ^ self.

ZoneOffset >> microseconds
  ^ microseconds.

ZoneOffset >> name
  ^ name.

"The offset as +HH:MM[:SS[.ffffff]]."
ZoneOffset >> isoOffset
  ^ ZoneOffset _isoOffset: microseconds.

ZoneOffset >> = anObject
  ^ (anObject isKindOf: ZoneOffset) and: [microseconds = anObject microseconds].

ZoneOffset >> hash
  ^ microseconds hash.

ZoneOffset >> printOn: aStream
  aStream nextPutAll: name.

"=== ZoneName ================================================================"

"An IANA time zone, by key (America/Argentina/Buenos_Aires). The key is
 kept as text: there is no database, so no rules and no offsets."
_CalendarValue subclass: #ZoneName instanceVariableNames: 'key'.

ZoneName class >> key: aString
  (aString isString and: [aString isEmpty not]) ifFalse: [
    ^ Error signal: 'ZoneName: the key must be a non-empty String, not ' , aString printString ].
  ^ self basicNew _setKey: aString.

ZoneName >> _setKey: aString
  key isNil ifFalse: [^ Error signal: 'ZoneName is immutable: ' , self printString].
  key := aString.
  ^ self.

ZoneName >> key
  ^ key.

ZoneName >> = anObject
  ^ (anObject isKindOf: ZoneName) and: [key = anObject key].

ZoneName >> hash
  ^ key hash.

ZoneName >> printOn: aStream
  aStream nextPutAll: key.

"=== CalendarDate ============================================================"

"A day of the proleptic Gregorian calendar, years 1 to 9999 (Python's date)."
_CalendarValue subclass: #CalendarDate instanceVariableNames: 'year month day'.

CalendarDate class >> year: y month: m day: d
  self _checkYear: y month: m day: d.
  ^ self basicNew _setYear: y month: m day: d.

"The date of day number anInteger (1 is 0001-01-01); Python's
 date.fromordinal."
CalendarDate class >> fromOrdinal: anInteger
  | n n400 n100 n4 n1 year month |
  self _check: anInteger from: 1 to: 3652059 named: 'the ordinal'.
  n := anInteger - 1.
  n400 := n // 146097. n := n \\ 146097.
  n100 := n // 36524. n := n \\ 36524.
  n4 := n // 1461. n := n \\ 1461.
  n1 := n // 365. n := n \\ 365.
  year := (n400 * 400) + 1 + (n100 * 100) + (n4 * 4) + n1.
  (n1 = 4 or: [n100 = 4]) ifTrue: [^ self year: year - 1 month: 12 day: 31].
  month := 12.
  [(self _daysBeforeMonth: month year: year) > n] whileTrue: [month := month - 1].
  ^ self year: year month: month day: n - (self _daysBeforeMonth: month year: year) + 1.

CalendarDate >> _setYear: y month: m day: d
  year isNil ifFalse: [^ Error signal: 'CalendarDate is immutable: ' , self printString].
  year := y.
  month := m.
  day := d.
  ^ self.

CalendarDate >> year
  ^ year.

CalendarDate >> month
  ^ month.

CalendarDate >> day
  ^ day.

"The day number: 0001-01-01 is 1 (Python's toordinal)."
CalendarDate >> ordinal
  ^ CalendarDate _ordinalYear: year month: month day: day.

CalendarDate >> = anObject
  ^ (anObject isKindOf: CalendarDate)
    and: [year = anObject year and: [month = anObject month and: [day = anObject day]]].

CalendarDate >> hash
  ^ self ordinal hash.

CalendarDate >> _orderWith: anObject
  (anObject isKindOf: CalendarDate) ifFalse: [^ self _cannotOrder: anObject].
  ^ self _sign: self ordinal - anObject ordinal.

"YYYY-MM-DD (Python's str(date))."
CalendarDate >> printOn: aStream
  aStream
    nextPutAll: (year printPaddedWith: $0 to: 4); nextPut: $-;
    nextPutAll: (month printPaddedWith: $0 to: 2); nextPut: $-;
    nextPutAll: (day printPaddedWith: $0 to: 2).

"=== TimeOfDay ==============================================================="

"A wall-clock time with fold and an optional zone: nil (naive), a
 ZoneOffset, or a ZoneName (Python's time with a ZoneInfo, which has no
 offset because a time has no date)."
_CalendarValue subclass: #TimeOfDay
  instanceVariableNames: 'hour minute second microsecond fold zone'.

TimeOfDay class >> hour: h minute: mi second: s microsecond: us fold: f zone: z
  self _checkHour: h minute: mi second: s microsecond: us fold: f.
  ((z isNil or: [z isKindOf: ZoneOffset]) or: [z isKindOf: ZoneName]) ifFalse: [
    ^ Error signal: 'TimeOfDay: the zone must be nil, a ZoneOffset or a ZoneName, not '
      , z printString ].
  ^ self basicNew _setHour: h minute: mi second: s microsecond: us fold: f zone: z.

"A naive time, microsecond 0, fold 0."
TimeOfDay class >> hour: h minute: mi second: s
  ^ self hour: h minute: mi second: s microsecond: 0 fold: 0 zone: nil.

TimeOfDay >> _setHour: h minute: mi second: s microsecond: us fold: f zone: z
  hour isNil ifFalse: [^ Error signal: 'TimeOfDay is immutable: ' , self printString].
  hour := h. minute := mi. second := s. microsecond := us. fold := f. zone := z.
  ^ self.

TimeOfDay >> hour
  ^ hour.

TimeOfDay >> minute
  ^ minute.

TimeOfDay >> second
  ^ second.

TimeOfDay >> microsecond
  ^ microsecond.

TimeOfDay >> fold
  ^ fold.

TimeOfDay >> zone
  ^ zone.

TimeOfDay >> microsecondsSinceMidnight
  ^ (((hour * 60) + minute) * 60 + second) * 1000000 + microsecond.

"The offset in microseconds of a fixed-offset time; nil for a naive time and
 for a time in a ZoneName (which has no offset)."
TimeOfDay >> _offset
  ^ (zone isKindOf: ZoneOffset) ifTrue: [zone microseconds] ifFalse: [nil].

"What = and the order compare: the wall clock, minus the offset if any."
TimeOfDay >> _key
  | offset |
  offset := self _offset.
  ^ offset isNil
    ifTrue: [self microsecondsSinceMidnight]
    ifFalse: [self microsecondsSinceMidnight - offset].

TimeOfDay >> = anObject
  (anObject isKindOf: TimeOfDay) ifFalse: [^ false].
  self _offset isNil = anObject _offset isNil ifFalse: [^ false].
  ^ self _key = anObject _key.

TimeOfDay >> hash
  ^ self _key hash.

TimeOfDay >> _orderWith: anObject
  ((anObject isKindOf: TimeOfDay)
      and: [self _offset isNil = anObject _offset isNil])
    ifFalse: [^ self _cannotOrder: anObject].
  ^ self _sign: self _key - anObject _key.

"Python's str(time): HH:MM:SS[.ffffff], then the offset; a ZoneName key
 follows in brackets."
TimeOfDay >> printOn: aStream
  self class _printHour: hour minute: minute second: second microsecond: microsecond on: aStream.
  (zone isKindOf: ZoneOffset) ifTrue: [aStream nextPutAll: zone isoOffset].
  (zone isKindOf: ZoneName) ifTrue: [
    aStream nextPut: $[; nextPutAll: zone key; nextPut: $]].

"=== DateTime ================================================================"

"A date and a time: naive (zone nil), at a fixed offset (a ZoneOffset; the
 fields are the wall clock), or the instant form (a ZoneName; the fields are
 the UTC instant and fold is 0)."
_CalendarValue subclass: #DateTime
  instanceVariableNames: 'year month day hour minute second microsecond fold zone local'.

"A naive (zone nil) or fixed-offset (a ZoneOffset) value from its wall
 clock. A ZoneName signals TimeZoneUnavailable: without a time-zone database
 the wall clock cannot be turned into an instant (use utcFields:zoneName:)."
DateTime class >> year: y month: m day: d hour: h minute: mi second: s microsecond: us fold: f zone: z
  self _checkYear: y month: m day: d.
  self _checkHour: h minute: mi second: s microsecond: us fold: f.
  (z isKindOf: ZoneName) ifTrue: [
    ^ TimeZoneUnavailable signal: 'DateTime: no time-zone database (STATUS D50) to turn the wall clock '
      , (self _fieldsText: { y. m. d. h. mi. s. us }) , ' in ' , z key
      , ' into an instant; build it with utcFields:zoneName:' ].
  (z isNil or: [z isKindOf: ZoneOffset]) ifFalse: [
    ^ Error signal: 'DateTime: the zone must be nil, a ZoneOffset or a ZoneName, not '
      , z printString ].
  ^ self basicNew _setYear: y month: m day: d hour: h minute: mi second: s
      microsecond: us fold: f zone: z.

"The instant form: anArray holds the UTC instant as year, month, day, hour,
 minute, second, microsecond; fold is 0 and the zone is aZoneName."
DateTime class >> utcFields: anArray zoneName: aZoneName
  ((anArray isKindOf: Array) and: [anArray size = 7]) ifFalse: [
    ^ Error signal: 'DateTime utcFields: an Array of 7 fields was expected, not '
      , anArray printString ].
  (aZoneName isKindOf: ZoneName) ifFalse: [
    ^ Error signal: 'DateTime utcFields:zoneName: a ZoneName was expected, not '
      , aZoneName printString ].
  self _checkYear: (anArray at: 1) month: (anArray at: 2) day: (anArray at: 3).
  self _checkHour: (anArray at: 4) minute: (anArray at: 5) second: (anArray at: 6)
    microsecond: (anArray at: 7) fold: 0.
  ^ self basicNew
      _setYear: (anArray at: 1) month: (anArray at: 2) day: (anArray at: 3)
      hour: (anArray at: 4) minute: (anArray at: 5) second: (anArray at: 6)
      microsecond: (anArray at: 7) fold: 0 zone: aZoneName.

"The value of the UTC instant anInteger (microseconds since 0001-01-01T00:00)
 at a fixed offset; with a ZoneName, the instant form. Its wall clock must
 lie in years 1..9999."
DateTime class >> utcOrdinalMicroseconds: anInteger zone: aZone
  | lcl |
  anInteger isInteger ifFalse: [
    ^ Error signal: 'DateTime utcOrdinalMicroseconds: an Integer was expected, not ' , anInteger printString ].
  (aZone isKindOf: ZoneName) ifTrue: [^ self _fromLocal: anInteger zone: aZone].
  (aZone isKindOf: ZoneOffset) ifFalse: [
    ^ Error signal: 'DateTime utcOrdinalMicroseconds:zone: a ZoneOffset or a ZoneName was expected, not '
      , aZone printString ].
  lcl := anInteger + aZone microseconds.
  ^ self _fromLocal: lcl zone: aZone.

"The Unix epoch, 1970-01-01T00:00Z, in microseconds since 0001-01-01T00:00:
 the offset between the Unix and the ordinal counts."
DateTime class >> unixEpochOrdinalMicroseconds
  ^ 62135596800000000.

"The value of the UTC instant anInteger, in microseconds since
 1970-01-01T00:00Z (the count of Time utcMicroseconds), at aZone, as
 utcOrdinalMicroseconds:zone:."
DateTime class >> fromUnixMicroseconds: anInteger zone: aZone
  anInteger isInteger ifFalse: [
    ^ Error signal: 'DateTime fromUnixMicroseconds: an Integer was expected, not '
      , anInteger printString ].
  ^ self utcOrdinalMicroseconds: anInteger + self unixEpochOrdinalMicroseconds zone: aZone.

"Now, in UTC (zone ZoneOffset utc), from Time utcMicroseconds (protoST 0.9.1)."
DateTime class >> nowUTC
  ^ self fromUnixMicroseconds: Time utcMicroseconds zone: ZoneOffset utc.

DateTime class >> _fromLocal: anInteger zone: aZone
  | days rest date |
  self _check: anInteger from: 0 to: 315537897599999999 named: 'the wall clock in microseconds'.
  days := anInteger // 86400000000.
  rest := anInteger \\ 86400000000.
  date := CalendarDate fromOrdinal: days + 1.
  ^ self basicNew
      _setYear: date year month: date month day: date day
      hour: rest // 3600000000 minute: rest // 60000000 \\ 60
      second: rest // 1000000 \\ 60 microsecond: rest \\ 1000000
      fold: 0 zone: aZone.

DateTime class >> _fieldsText: anArray
  ^ String streamContents: [:w |
    w nextPutAll: ((anArray at: 1) printPaddedWith: $0 to: 4); nextPut: $-;
      nextPutAll: ((anArray at: 2) printPaddedWith: $0 to: 2); nextPut: $-;
      nextPutAll: ((anArray at: 3) printPaddedWith: $0 to: 2); nextPut: $ .
    self _printHour: (anArray at: 4) minute: (anArray at: 5) second: (anArray at: 6)
      microsecond: (anArray at: 7) on: w].

DateTime >> _setYear: y month: m day: d hour: h minute: mi second: s microsecond: us fold: f zone: z
  local isNil ifFalse: [^ Error signal: 'DateTime is immutable: ' , self printString].
  year := y. month := m. day := d.
  hour := h. minute := mi. second := s. microsecond := us.
  fold := f. zone := z.
  local := ((CalendarDate _ordinalYear: y month: m day: d) - 1) * 86400000000
    + ((((h * 60) + mi) * 60 + s) * 1000000 + us).
  ^ self.

DateTime >> year
  ^ year.

DateTime >> month
  ^ month.

DateTime >> day
  ^ day.

DateTime >> hour
  ^ hour.

DateTime >> minute
  ^ minute.

DateTime >> second
  ^ second.

DateTime >> microsecond
  ^ microsecond.

DateTime >> fold
  ^ fold.

"nil, a ZoneOffset or a ZoneName."
DateTime >> zone
  ^ zone.

DateTime >> isNaive
  ^ zone isNil.

"True for the instant form (a ZoneName): the fields are the UTC instant."
DateTime >> isInstantForm
  ^ zone isKindOf: ZoneName.

"The fields' microseconds since 0001-01-01T00:00."
DateTime >> localOrdinalMicroseconds
  ^ local.

"The UTC instant in microseconds since 0001-01-01T00:00: the fields minus the
 offset, or the fields of the instant form. A naive value signals Error."
DateTime >> utcOrdinalMicroseconds
  zone isNil ifTrue: [
    ^ Error signal: 'DateTime: a naive value has no UTC instant: ' , self printString ].
  self isInstantForm ifTrue: [^ local].
  ^ local - zone microseconds.

"The UTC instant in microseconds since 1970-01-01T00:00Z (negative before
 it). A naive value signals Error."
DateTime >> unixMicroseconds
  ^ self utcOrdinalMicroseconds - DateTime unixEpochOrdinalMicroseconds.

"The receiver, whose fields are its wall clock; the instant form signals
 TimeZoneUnavailable (no time-zone database, STATUS D50)."
DateTime >> wallClock
  self isInstantForm ifTrue: [
    ^ TimeZoneUnavailable signal: 'DateTime: no time-zone database (STATUS D50) to show the instant '
      , self printString , ' as a wall clock in ' , zone key ].
  ^ self.

"The wall-clock date (Python's dt.date())."
DateTime >> date
  self wallClock.
  ^ CalendarDate year: year month: month day: day.

"The wall-clock time, naive (Python's dt.time())."
DateTime >> time
  self wallClock.
  ^ TimeOfDay hour: hour minute: minute second: second microsecond: microsecond fold: fold zone: nil.

DateTime >> = anObject
  (anObject isKindOf: DateTime) ifFalse: [^ false].
  self isNaive = anObject isNaive ifFalse: [^ false].
  self isNaive ifTrue: [^ local = anObject localOrdinalMicroseconds].
  ^ self utcOrdinalMicroseconds = anObject utcOrdinalMicroseconds.

DateTime >> hash
  self isNaive ifTrue: [^ local hash].
  ^ self utcOrdinalMicroseconds hash.

DateTime >> _orderWith: anObject
  ((anObject isKindOf: DateTime) and: [self isNaive = anObject isNaive])
    ifFalse: [^ self _cannotOrder: anObject].
  self isNaive ifTrue: [^ self _sign: local - anObject localOrdinalMicroseconds].
  ^ self _sign: self utcOrdinalMicroseconds - anObject utcOrdinalMicroseconds.

"Python's str(datetime): YYYY-MM-DD HH:MM:SS[.ffffff], then the offset. The
 instant form prints its UTC fields, Z and the zone key in brackets (as in
 RFC 9557: the instant is known, the local offset is not)."
DateTime >> printOn: aStream
  aStream nextPutAll: (DateTime _fieldsText: { year. month. day. hour. minute. second. microsecond }).
  (zone isKindOf: ZoneOffset) ifTrue: [aStream nextPutAll: zone isoOffset].
  self isInstantForm ifTrue: [aStream nextPutAll: 'Z['; nextPutAll: zone key; nextPut: $]].

"=== TimeSpan ================================================================"

"A signed duration in microseconds, kept as Python's timedelta keeps it:
 days (|days| <= 999999999), 0 <= seconds < 86400, 0 <= microseconds < 10^6."
_CalendarValue subclass: #TimeSpan instanceVariableNames: 'days seconds microseconds total'.

"Normalised exactly as timedelta(days=d, seconds=s, microseconds=us), for
 Integer arguments."
TimeSpan class >> days: d seconds: s microseconds: us
  (d isInteger and: [s isInteger and: [us isInteger]]) ifFalse: [
    ^ Error signal: 'TimeSpan: days, seconds and microseconds must be Integers, not '
      , { d. s. us } printString ].
  ^ self microseconds: (d * 86400000000) + (s * 1000000) + us.

TimeSpan class >> microseconds: anInteger
  | d rest |
  anInteger isInteger ifFalse: [
    ^ Error signal: 'TimeSpan: microseconds must be an Integer, not ' , anInteger printString ].
  d := anInteger // 86400000000.
  d abs <= 999999999 ifFalse: [
    ^ Error signal: 'TimeSpan: |days| must be at most 999999999, not ' , d printString ].
  rest := anInteger \\ 86400000000.
  ^ self basicNew _setDays: d seconds: rest // 1000000 microseconds: rest \\ 1000000 total: anInteger.

TimeSpan >> _setDays: d seconds: s microseconds: us total: n
  total isNil ifFalse: [^ Error signal: 'TimeSpan is immutable: ' , self printString].
  days := d. seconds := s. microseconds := us. total := n.
  ^ self.

TimeSpan >> days
  ^ days.

TimeSpan >> seconds
  ^ seconds.

TimeSpan >> microseconds
  ^ microseconds.

TimeSpan >> totalMicroseconds
  ^ total.

TimeSpan >> = anObject
  ^ (anObject isKindOf: TimeSpan) and: [total = anObject totalMicroseconds].

TimeSpan >> hash
  ^ total hash.

TimeSpan >> _orderWith: anObject
  (anObject isKindOf: TimeSpan) ifFalse: [^ self _cannotOrder: anObject].
  ^ self _sign: total - anObject totalMicroseconds.

"Python's str(timedelta): [D day[s], ]H:MM:SS[.ffffff]."
TimeSpan >> printOn: aStream
  days = 0 ifFalse: [
    aStream nextPutAll: days printString; nextPutAll: ' day'.
    days abs = 1 ifFalse: [aStream nextPut: $s].
    aStream nextPutAll: ', '].
  aStream nextPutAll: (seconds // 3600) printString; nextPut: $:;
    nextPutAll: (seconds // 60 \\ 60 printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (seconds \\ 60 printPaddedWith: $0 to: 2).
  microseconds = 0 ifFalse: [
    aStream nextPut: $.; nextPutAll: (microseconds printPaddedWith: $0 to: 6)].
