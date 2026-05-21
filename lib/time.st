"-- time.st: the Time standard-library module --

Provides wall-clock access and a small timestamp / duration object model.
A thin idiomatic protoST face over two C++ clock primitives:

  __currentMillis    — epoch milliseconds (system clock, wall time).
  __monotonicMillis  — milliseconds from a monotonic steady clock.

Both primitives are bootstrapped onto Object, so this module is pure protoST
on top of them — no per-module primitive loading.

SCOPE
  This module is deliberately minimal: wall-clock access plus basic
  timestamp / duration arithmetic. There are NO timezones and NO calendar
  (no year/month/day decomposition). A Timestamp is an opaque instant; a
  Duration is an opaque span. They compose, compare and subtract.

USAGE
  m := Import from: 'time'.
  t := m Time now.                  => a Timestamp for the current instant
  t asMilliseconds.                 => epoch milliseconds (a positive integer)
  d := m Duration seconds: 90.
  d asSeconds.                      => 90
  later := t + d.                   => a Timestamp 90 s after t
  (later - t) asSeconds.            => 90
  m Time millisecondsToRun: [ ... ] => elapsed monotonic milliseconds"

"=== Time ==================================================================="

"Time is a namespace class — it owns the class-side entry points `now` and
 `millisecondsToRun:`. It is never instantiated."
Object subclass: #Time
  instanceVariableNames: ''.

"A Timestamp for the current instant, taken from the system (wall) clock."
Time class >> now
  ^ Timestamp fromMilliseconds: self __currentMillis.

"Evaluate aBlock and answer the elapsed wall time in milliseconds, measured
 with the monotonic clock so an NTP step cannot corrupt the result. The
 answer is a non-negative integer (the monotonic clock never runs backwards)."
Time class >> millisecondsToRun: aBlock
  | start |
  start := self __monotonicMillis.
  aBlock value.
  ^ self __monotonicMillis - start.

"=== Timestamp =============================================================="

"Timestamp wraps a single instant as an integer count of epoch milliseconds.
 It is immutable: arithmetic answers fresh Timestamp / Duration objects."
Object subclass: #Timestamp
  instanceVariableNames: 'millis'.

"Class-side constructor: a Timestamp at the given epoch-millisecond count."
Timestamp class >> fromMilliseconds: anInteger
  | t |
  t := self new.
  t setMillis: anInteger.
  ^ t.

"Private initialiser used by the constructor."
Timestamp >> setMillis: anInteger
  millis := anInteger.

"The instant as epoch milliseconds — the raw integer."
Timestamp >> asMilliseconds
  ^ millis.

"The instant as whole epoch seconds (truncated)."
Timestamp >> asSeconds
  ^ millis // 1000.

"True when the receiver is the same instant as aTimestamp."
Timestamp >> = aTimestamp
  ^ millis = aTimestamp asMilliseconds.

"True when the receiver is strictly earlier than aTimestamp."
Timestamp >> < aTimestamp
  ^ millis < aTimestamp asMilliseconds.

"True when the receiver is no later than aTimestamp."
Timestamp >> <= aTimestamp
  ^ millis <= aTimestamp asMilliseconds.

"True when the receiver is strictly later than aTimestamp."
Timestamp >> > aTimestamp
  ^ millis > aTimestamp asMilliseconds.

"True when the receiver is no earlier than aTimestamp."
Timestamp >> >= aTimestamp
  ^ millis >= aTimestamp asMilliseconds.

"The difference between two Timestamps — a Duration. A later receiver minus
 an earlier argument yields a positive Duration."
Timestamp >> - aTimestamp
  ^ Duration milliseconds: millis - aTimestamp asMilliseconds.

"This instant advanced by aDuration — a fresh Timestamp."
Timestamp >> + aDuration
  ^ Timestamp fromMilliseconds: millis + aDuration asMilliseconds.

"=== Duration ==============================================================="

"Duration wraps a span of time as an integer count of milliseconds. It is
 immutable; arithmetic answers fresh Duration objects."
Object subclass: #Duration
  instanceVariableNames: 'millis'.

"Class-side constructor: a Duration of the given millisecond span."
Duration class >> milliseconds: anInteger
  | d |
  d := self new.
  d setMillis: anInteger.
  ^ d.

"Class-side constructor: a Duration of the given number of seconds."
Duration class >> seconds: anInteger
  ^ self milliseconds: anInteger * 1000.

"Class-side constructor: a Duration of the given number of minutes."
Duration class >> minutes: anInteger
  ^ self milliseconds: anInteger * 60000.

"Private initialiser used by the constructors."
Duration >> setMillis: anInteger
  millis := anInteger.

"The span as a millisecond count — the raw integer."
Duration >> asMilliseconds
  ^ millis.

"The span as whole seconds (truncated)."
Duration >> asSeconds
  ^ millis // 1000.

"The sum of two Durations — a fresh Duration."
Duration >> + aDuration
  ^ Duration milliseconds: millis + aDuration asMilliseconds.

"The difference of two Durations — a fresh Duration."
Duration >> - aDuration
  ^ Duration milliseconds: millis - aDuration asMilliseconds.

"True when the two Durations span the same number of milliseconds."
Duration >> = aDuration
  ^ millis = aDuration asMilliseconds.

"True when the receiver is strictly shorter than aDuration."
Duration >> < aDuration
  ^ millis < aDuration asMilliseconds.

"True when the receiver is no longer than aDuration."
Duration >> <= aDuration
  ^ millis <= aDuration asMilliseconds.

"True when the receiver is strictly longer than aDuration."
Duration >> > aDuration
  ^ millis > aDuration asMilliseconds.

"True when the receiver is no shorter than aDuration."
Duration >> >= aDuration
  ^ millis >= aDuration asMilliseconds.
