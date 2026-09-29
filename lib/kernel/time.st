"Kernel: time and randomness. Time and Date read the local wall clock; the
 monotonic millisecond clock times code (timeToRun, millisecondsToRun:) and
 answers a Duration. Random is the Park-Miller minimal standard generator
 (the classic Smalltalk-80 Random), seeded from the system's entropy source
 unless a seed is given."

"--- Duration -------------------------------------------------------------------"

Object subclass: #Duration instanceVariableNames: 'milliseconds'.

Duration class >> milliseconds: anInteger
  ^ self basicNew setMilliseconds: anInteger.

Duration class >> seconds: aNumber
  ^ self milliseconds: (aNumber * 1000) rounded.

Duration >> setMilliseconds: anInteger
  milliseconds := anInteger.

Duration >> asMilliseconds
  ^ milliseconds.

Duration >> asSeconds
  ^ milliseconds // 1000.

Duration >> totalSeconds
  ^ milliseconds / 1000.

Duration >> + aDuration
  ^ Duration milliseconds: milliseconds + aDuration asMilliseconds.

Duration >> - aDuration
  ^ Duration milliseconds: milliseconds - aDuration asMilliseconds.

Duration >> < aDuration
  ^ milliseconds < aDuration asMilliseconds.

Duration >> = aDuration
  ^ (aDuration isKindOf: Duration) and: [milliseconds = aDuration asMilliseconds].

Duration >> hash
  ^ milliseconds hash.

"Printed as Pharo does: days:hours:minutes:seconds.milliseconds."
Duration >> printOn: aStream
  | ms s |
  ms := milliseconds abs.
  milliseconds < 0 ifTrue: [aStream nextPut: $-].
  s := ms // 1000.
  aStream
    print: s // 86400; nextPut: $:;
    nextPutAll: (s // 3600 \\ 24 printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (s // 60 \\ 60 printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (s \\ 60 printPaddedWith: $0 to: 2); nextPut: $.;
    nextPutAll: (ms \\ 1000 printPaddedWith: $0 to: 3).

"--- Time -----------------------------------------------------------------------"

Object subclass: #Time instanceVariableNames: 'hours minutes seconds'.

Time class >> now
  | f |
  f := nil __localTimeFields.
  ^ self hour: (f at: 4) minute: (f at: 5) second: (f at: 6).

Time class >> hour: h minute: m second: s
  ^ self basicNew setHours: h minutes: m seconds: s.

Time class >> millisecondClockValue
  ^ nil __clockMilliseconds.

Time class >> microsecondClockValue
  ^ nil __clockMicroseconds.

Time class >> millisecondsToRun: aBlock
  | start |
  start := self millisecondClockValue.
  aBlock value.
  ^ self millisecondClockValue - start.

Time >> setHours: h minutes: m seconds: s
  hours := h. minutes := m. seconds := s.

Time >> hours
  ^ hours.

Time >> minutes
  ^ minutes.

Time >> seconds
  ^ seconds.

Time >> asSeconds
  ^ hours * 3600 + (minutes * 60) + seconds.

Time >> = aTime
  ^ (aTime isKindOf: Time) and: [self asSeconds = aTime asSeconds].

Time >> hash
  ^ self asSeconds hash.

Time >> printOn: aStream
  aStream
    nextPutAll: (hours printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (minutes printPaddedWith: $0 to: 2); nextPut: $:;
    nextPutAll: (seconds printPaddedWith: $0 to: 2).

"--- Date -----------------------------------------------------------------------"

Object subclass: #Date instanceVariableNames: 'year month day weekday'.

Date class >> today
  | f |
  f := nil __localTimeFields.
  ^ self basicNew setYear: (f at: 1) month: (f at: 2) day: (f at: 3) weekday: (f at: 7).

Date class >> year: y month: m day: d
  ^ self basicNew setYear: y month: m day: d weekday: nil.

Date >> setYear: y month: m day: d weekday: w
  year := y. month := m. day := d. weekday := w.

Date >> year
  ^ year.

Date >> monthIndex
  ^ month.

Date >> dayOfMonth
  ^ day.

"1 = Sunday, as in Smalltalk-80 (nil for a Date built from its parts)."
Date >> dayOfWeek
  ^ weekday.

Date >> monthName
  ^ #('January' 'February' 'March' 'April' 'May' 'June' 'July' 'August'
      'September' 'October' 'November' 'December') at: month.

Date >> = aDate
  ^ (aDate isKindOf: Date)
    and: [year = aDate year and: [month = aDate monthIndex and: [day = aDate dayOfMonth]]].

Date >> hash
  ^ (year * 400 + (month * 31) + day) hash.

"ISO 8601 (Pharo prints '29 September 2026'; protoST prints 2026-09-29)."
Date >> printOn: aStream
  aStream
    print: year; nextPut: $-;
    nextPutAll: (month printPaddedWith: $0 to: 2); nextPut: $-;
    nextPutAll: (day printPaddedWith: $0 to: 2).

"--- timing code ----------------------------------------------------------------"

BlockClosure >> timeToRun
  ^ Duration milliseconds: (Time millisecondsToRun: self).

"--- Random ---------------------------------------------------------------------"

Object subclass: #Random instanceVariableNames: 'seed'.

Random class >> new
  ^ self seed: nil __entropySeed.

Random class >> seed: anInteger
  ^ self basicNew setSeed: anInteger.

Random >> setSeed: anInteger
  seed := anInteger \\ 2147483647.
  seed = 0 ifTrue: [seed := 1].

"A Float uniformly distributed in [0, 1)."
Random >> next
  seed := seed * 16807 \\ 2147483647.
  ^ (seed - 1) / 2147483646.0.

"An Integer uniformly distributed in 1 .. anInteger."
Random >> nextInt: anInteger
  ^ (self next * anInteger) truncated + 1.

Random >> between: low and: high
  ^ low + (self nextInt: high - low + 1) - 1.

Collection >> atRandom
  | items |
  items := self asArray.
  ^ items at: (Random new nextInt: items size).

Integer >> atRandom
  ^ Random new nextInt: self.
