"EXPECT: #(true true true true true true true '0:00:00:01.500' 1500 true true)"
"Time, Date, Duration and Random: the wall clock answers plausible fields,
 timeToRun answers a Duration printed as Pharo does, and a seeded Random is
 reproducible and within range."
| t d r1 r2 a b ints |
t := Time now.
d := Date today.
r1 := Random seed: 42.
r2 := Random seed: 42.
a := (1 to: 5) collect: [:i | r1 next].
b := (1 to: 5) collect: [:i | r2 next].
ints := (1 to: 200) collect: [:i | r1 nextInt: 6].
{ t hours between: 0 and: 23. t seconds between: 0 and: 59.
  d year >= 2026. d monthIndex between: 1 and: 12.
  a = b. (a allSatisfy: [:v | v >= 0 and: [v < 1]]).
  (ints allSatisfy: [:v | v between: 1 and: 6]).
  (Duration milliseconds: 1500) printString. (Duration milliseconds: 1500) asMilliseconds.
  [3 + 4] timeToRun asMilliseconds >= 0.
  (Random new next between: 0 and: 1) }
