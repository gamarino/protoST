"EXPECT: true"
"§13 stdlib: Timestamp + Duration then - Timestamp recovers the Duration —
the two object types compose consistently."
m := Import from: 'time'.
t := m Time now.
d := m Duration seconds: 90.
later := t + d.
(later - t) asMilliseconds = d asMilliseconds.
