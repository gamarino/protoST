"EXPECT: #(#(#KSortedCollection 1 1) #(#KWriteStream 1 1) #(#KRandom 1 1) #('KUUID' 1 1) 'the constant ''k'' of a KSortedCollection is not set at the end of construction (sortBlock:)' 'the constant ''k'' of a KWriteStream is not set at the end of construction (on:)' 'the constant ''k'' of a KWriteStream is not set at the end of construction (with:)' 'the constant ''k'' of a KReadStream is not set at the end of construction (on:)' 'the constant ''k'' of a KRandom is not set at the end of construction (seed:)' 'the constant ''k'' of a KPoint is not set at the end of construction (x:y:)' 'the constant ''k'' of a KFraction is not set at the end of construction (numerator:denominator:)' 'the constant ''k'' of a KDuration is not set at the end of construction (milliseconds:)' 'the constant ''k'' of a KTime is not set at the end of construction (hour:minute:second:)' 'the constant ''k'' of a KDate is not set at the end of construction (year:month:day:)' 'the constant ''k'' of a KDate is not set at the end of construction (today)' 'the constant ''k'' of a KNumericInterval is not set at the end of construction (from:to:by:)' 'the constant ''k'' of a KFileReference is not set at the end of construction (path:)' 'the constant ''k'' of a KIOStream is not set at the end of construction (onDescriptor:)' 'the constant ''k'' of a KMessagePackExt is not set at the end of construction (type:data:)' 'the constant ''k'' of a KMessagePackMap is not set at the end of construction (__fromAssociations:)' 'the constant ''k'' of a KUUID is not set at the end of construction (fromBytes:)' 'the constant ''k'' of a KUUID is not set at the end of construction (fromString:)' 'the constant ''k'' of a KZoneOffset is not set at the end of construction (microseconds:name:)' 'the constant ''k'' of a KZoneName is not set at the end of construction (key:)' 'the constant ''k'' of a KCalendarDate is not set at the end of construction (year:month:day:)' 'the constant ''k'' of a KTimeOfDay is not set at the end of construction (hour:minute:second:microsecond:fold:zone:)' 'the constant ''k'' of a KDateTime is not set at the end of construction (year:month:day:hour:minute:second:microsecond:fold:zone:)' 'the constant ''k'' of a KDateTime is not set at the end of construction (utcFields:zoneName:)' 'the constant ''k'' of a KTimeSpan is not set at the end of construction (microseconds:)' 'the constant ''k'' of a KDecimal is not set at the end of construction (sign:coefficient:exponent:)' 'the constant ''k'' of a KDecimal is not set at the end of construction (infinity:)' 'the constant ''k'' of a KDecimal is not set at the end of construction (nan:signaling:sign:)' 0 0 1 3)"
"Spec §6.2 and the Task 8 rulings: class-side constructors.
 - new (SortedCollection, WriteStream, Random, UUID) sends initialize once,
   as Object class>>new does, then checks the constants: a user subclass
   whose initialize sets its constant builds fine.
 - Every other kernel or library constructor that builds with basicNew and a
   setter keeps its 0.16.1 behaviour, which (checked on 0.16.1) never sent
   initialize, as in Pharo, where initialize belongs to new. It only appends
   the end-of-construction check, so the same subclass gets M13 naming that
   constructor.
 - A counter pins that Point x:y: and SortedCollection sortBlock: do not
   send initialize, while SortedCollection new sends it once.
 The socket constructors of net (listenOn:host:, bindTo:host:) and BinaryFile
 open:options: get the same check; they are not exercised here because they
 open operating-system resources."
| m1 m2 m3 |
m1 := Import from: 'uuid'.
m2 := Import from: 'calendar'.
m3 := Import from: 'decimal'.
SortedCollection yourself subclass: #KSortedCollection
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KSortedCollection >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KSortedCollection >> probe
  ^ {self class name. k. inits}

WriteStream yourself subclass: #KWriteStream
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KWriteStream >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KWriteStream >> probe
  ^ {self class name. k. inits}

Random yourself subclass: #KRandom
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KRandom >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KRandom >> probe
  ^ {self class name. k. inits}

UUID yourself subclass: #KUUID
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KUUID >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KUUID >> probe
  ^ {self class name. k. inits}

ReadStream yourself subclass: #KReadStream
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KReadStream >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KReadStream >> probe
  ^ {self class name. k. inits}

Point yourself subclass: #KPoint
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KPoint >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KPoint >> probe
  ^ {self class name. k. inits}

Fraction yourself subclass: #KFraction
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KFraction >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KFraction >> probe
  ^ {self class name. k. inits}

Duration yourself subclass: #KDuration
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KDuration >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KDuration >> probe
  ^ {self class name. k. inits}

Time yourself subclass: #KTime
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KTime >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KTime >> probe
  ^ {self class name. k. inits}

Date yourself subclass: #KDate
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KDate >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KDate >> probe
  ^ {self class name. k. inits}

NumericInterval yourself subclass: #KNumericInterval
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KNumericInterval >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KNumericInterval >> probe
  ^ {self class name. k. inits}

FileReference yourself subclass: #KFileReference
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KFileReference >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KFileReference >> probe
  ^ {self class name. k. inits}

IOStream yourself subclass: #KIOStream
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KIOStream >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KIOStream >> probe
  ^ {self class name. k. inits}

MessagePackExt yourself subclass: #KMessagePackExt
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KMessagePackExt >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KMessagePackExt >> probe
  ^ {self class name. k. inits}

MessagePackMap yourself subclass: #KMessagePackMap
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KMessagePackMap >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KMessagePackMap >> probe
  ^ {self class name. k. inits}

ZoneOffset yourself subclass: #KZoneOffset
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KZoneOffset >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KZoneOffset >> probe
  ^ {self class name. k. inits}

ZoneName yourself subclass: #KZoneName
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KZoneName >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KZoneName >> probe
  ^ {self class name. k. inits}

CalendarDate yourself subclass: #KCalendarDate
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KCalendarDate >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KCalendarDate >> probe
  ^ {self class name. k. inits}

TimeOfDay yourself subclass: #KTimeOfDay
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KTimeOfDay >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KTimeOfDay >> probe
  ^ {self class name. k. inits}

DateTime yourself subclass: #KDateTime
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KDateTime >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KDateTime >> probe
  ^ {self class name. k. inits}

TimeSpan yourself subclass: #KTimeSpan
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KTimeSpan >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KTimeSpan >> probe
  ^ {self class name. k. inits}

Decimal yourself subclass: #KDecimal
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
KDecimal >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
KDecimal >> probe
  ^ {self class name. k. inits}

Point yourself subclass: #CountPoint instanceVariableNames: 'inits'.
CountPoint >> initialize
  inits := (inits ifNil: [0]) + 1
CountPoint >> inits
  ^ inits ifNil: [0]

SortedCollection yourself subclass: #CountSorted instanceVariableNames: 'inits'.
CountSorted >> initialize
  inits := (inits ifNil: [0]) + 1
CountSorted >> inits
  ^ inits ifNil: [0]

| results m13 |
results := OrderedCollection new.
m13 := [:block | block on: Error do: [:e | e messageText]].
results add: (KSortedCollection new) probe.
results add: (KWriteStream new) probe.
results add: (KRandom new) probe.
results add: (KUUID new) probe.
results add: (m13 value: [KSortedCollection sortBlock: [:a :b | a >= b]]).
results add: (m13 value: [KWriteStream on: String new]).
results add: (m13 value: [KWriteStream with: 'ab']).
results add: (m13 value: [KReadStream on: #(1 2)]).
results add: (m13 value: [KRandom seed: 42]).
results add: (m13 value: [KPoint x: 1 y: 2]).
results add: (m13 value: [KFraction numerator: 1 denominator: 2]).
results add: (m13 value: [KDuration milliseconds: 5]).
results add: (m13 value: [KTime hour: 1 minute: 2 second: 3]).
results add: (m13 value: [KDate year: 2026 month: 11 day: 23]).
results add: (m13 value: [KDate today]).
results add: (m13 value: [KNumericInterval from: 1 to: 5 by: 2]).
results add: (m13 value: [KFileReference path: '/tmp']).
results add: (m13 value: [KIOStream onDescriptor: 1]).
results add: (m13 value: [KMessagePackExt type: 1 data: (ByteArray new: 2)]).
results add: (m13 value: [KMessagePackMap __fromAssociations: #()]).
results add: (m13 value: [KUUID fromBytes: (ByteArray new: 16)]).
results add: (m13 value: [KUUID fromString: '12345678123456781234567812345678']).
results add: (m13 value: [KZoneOffset microseconds: 0 name: 'UTC']).
results add: (m13 value: [KZoneName key: 'UTC']).
results add: (m13 value: [KCalendarDate year: 2026 month: 11 day: 23]).
results add: (m13 value: [KTimeOfDay hour: 1 minute: 2 second: 3 microsecond: 0 fold: 0 zone: nil]).
results add: (m13 value: [KDateTime year: 2026 month: 11 day: 23 hour: 1 minute: 2 second: 3 microsecond: 0 fold: 0 zone: nil]).
results add: (m13 value: [KDateTime utcFields: #(2026 11 23 1 2 3 0) zoneName: (ZoneName key: 'UTC')]).
results add: (m13 value: [KTimeSpan microseconds: 5]).
results add: (m13 value: [KDecimal sign: 0 coefficient: 5 exponent: 0]).
results add: (m13 value: [KDecimal infinity: 0]).
results add: (m13 value: [KDecimal nan: 0 signaling: false sign: 0]).
results add: (CountPoint x: 1 y: 2) inits.
results add: (CountSorted sortBlock: [:a :b | a < b]) inits.
results add: CountSorted new inits.
results add: (Point x: 3 y: 4) x.
results asArray
