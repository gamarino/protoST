"EXPECT: 5 refused"
"§13 stdlib: calendar values refuse fields out of range with Error: month 13,
 30 February, year 0, year 10000 and an offset of 86400 * 10^6 microseconds."
| m n |
m := Import from: 'calendar'.
n := 0.
[m CalendarDate year: 2026 month: 13 day: 1] on: Error do: [:e | n := n + 1].
[m CalendarDate year: 2026 month: 2 day: 30] on: Error do: [:e | n := n + 1].
[m CalendarDate year: 0 month: 1 day: 1] on: Error do: [:e | n := n + 1].
[m CalendarDate year: 10000 month: 1 day: 1] on: Error do: [:e | n := n + 1].
[m ZoneOffset microseconds: 86400000000 name: 'X'] on: Error do: [:e | n := n + 1].
n printString , ' refused'.
