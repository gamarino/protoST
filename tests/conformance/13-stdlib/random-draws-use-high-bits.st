"EXPECT: #(true true true true true)"
"§13 stdlib: Random>>nextInt: and between:and: scale the generator's high bits
 into the range. The generator is a power-of-two-modulus LCG, whose bit k has
 period 2^(k+1): a draw taken as the state modulo the range (the 0.9.x code)
 alternated parity on every even range and repeated nextInt: 4 with period 4.
 Seeded, so the counts are exact on every run; the bounds only say what any
 uniform generator satisfies and the low-bit one did not.
 1. nextInt: 2 over 1000 draws: successive draws are equal about half the time
    (the low-bit code: never).
 2. between: 0 and: 1: the same, through between:and:.
 3. nextInt: 4: d(i) = d(i+4) about a quarter of the time (low-bit: always).
 4. between: 1 and: 10: every value appears 60 to 140 times in 1000 draws.
 5. a range wider than the state (2^40 values) still stays in range."
m := Import from: 'random'.
r := m Random seed: 20261111.
d := (1 to: 1000) collect: [ :i | r nextInt: 2 ].
same := (1 to: 999) inject: 0 into: [ :acc :i |
  (d at: i) = (d at: i + 1) ifTrue: [ acc + 1 ] ifFalse: [ acc ] ].
r := m Random seed: 7.
e := (1 to: 1000) collect: [ :i | r between: 0 and: 1 ].
same2 := (1 to: 999) inject: 0 into: [ :acc :i |
  (e at: i) = (e at: i + 1) ifTrue: [ acc + 1 ] ifFalse: [ acc ] ].
r := m Random seed: 42.
f := (1 to: 1000) collect: [ :i | r nextInt: 4 ].
period4 := (1 to: 996) inject: 0 into: [ :acc :i |
  (f at: i) = (f at: i + 4) ifTrue: [ acc + 1 ] ifFalse: [ acc ] ].
r := m Random seed: 99.
counts := Array new: 10 withAll: 0.
1000 timesRepeat: [ | v |
  v := r between: 1 and: 10.
  counts at: v put: (counts at: v) + 1 ].
r := m Random seed: 3.
wide := (1 to: 100) allSatisfy: [ :i | | v |
  v := r between: 0 and: 1099511627775.
  v >= 0 and: [ v <= 1099511627775 ] ].
{ same between: 400 and: 600.
  same2 between: 400 and: 600.
  period4 between: 150 and: 350.
  counts allSatisfy: [ :c | c between: 60 and: 140 ].
  wide }.
