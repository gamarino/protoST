"EXPECT: #(1 2 2 true 10 false 10)"
"Spec §6.5, conformance 76: plain variables take update:with: and
 compareAndSet:from:to:, as the deprecated CAS alias allowed on them."
Object subclass: #Box instanceVariableNames: 'count'.
Box >> count
  ^ count

| b |
b := Box new.
{b update: #count with: [:old | (old ifNil: [0]) + 1].
 b update: #count with: [:old | old + 1].
 b count.
 b compareAndSet: #count from: 2 to: 10.
 b count.
 b compareAndSet: #count from: 2 to: 11.
 b count}
