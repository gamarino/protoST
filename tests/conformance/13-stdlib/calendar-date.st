"EXPECT: 1 730179 3652059 739930 2021-07-29 0400-12-31 0401-01-01 2026-11-10 true true true false true"
"§13 stdlib: CalendarDate, the proleptic Gregorian date. Ordinals as CPython's
 date.toordinal() prints them (0001-01-01, 2000-02-29, 9999-12-31,
 2026-11-10); fromOrdinal: as date.fromordinal() (738000, 146097, 146098, the
 last day of a 400-year cycle and the first of the next); printString pads the
 year to four digits. Then: fromOrdinal: inverts ordinal over every day of
 1999-2001 and the ends of the range; equal dates are = with equal hashes; <
 orders by day; a date is never = to a non-date."
| m D d same roundtrip |
m := Import from: 'calendar'.
D := m CalendarDate.
d := [:y :mo :dd | D year: y month: mo day: dd].
same := (d value: 2026 value: 11 value: 10) = (d value: 2026 value: 11 value: 10)
  and: [(d value: 2026 value: 11 value: 10) hash = (d value: 2026 value: 11 value: 10) hash].
roundtrip := true.
((d value: 1999 value: 1 value: 1) ordinal to: (d value: 2001 value: 12 value: 31) ordinal) do: [:n |
  (D fromOrdinal: n) ordinal = n ifFalse: [roundtrip := false]].
#(1 3652059) do: [:n | (D fromOrdinal: n) ordinal = n ifFalse: [roundtrip := false]].
{ (d value: 1 value: 1 value: 1) ordinal.
  (d value: 2000 value: 2 value: 29) ordinal.
  (d value: 9999 value: 12 value: 31) ordinal.
  (d value: 2026 value: 11 value: 10) ordinal.
  (D fromOrdinal: 738000) printString.
  (D fromOrdinal: 146097) printString.
  (D fromOrdinal: 146098) printString.
  (d value: 2026 value: 11 value: 10) printString.
  same.
  roundtrip.
  (d value: 2026 value: 11 value: 9) < (d value: 2026 value: 11 value: 10).
  (d value: 2026 value: 11 value: 10) = '2026-11-10'.
  ((d value: 2026 value: 11 value: 10) year = 2026 and: [(d value: 2026 value: 11 value: 10) month = 11
     and: [(d value: 2026 value: 11 value: 10) day = 10]]) }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each displayString] ifFalse: [acc , ' ' , each displayString]].
