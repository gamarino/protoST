"EXPECT: 10.0 100.0 150.0 250.5 0.1 0.001 1234567.0 -2.5 1.0e16 1.0e-5 2.5e-10 1.0e100 0.30000000000000004 Float infinity Float nan true"
"Floats print as Smalltalk literals: shortest digits that read back exactly,
 positional between 1e-4 and 1e16, otherwise mantissa-e-exponent."
| out values finite |
out := WriteStream on: String new.
values := { 10.0. 100.0. 3 * 50.0. 250.5. 0.1. 0.001. 1234567.0. -2.5. 1.0e16. 1.0e-5. 2.5e-10. 1.0e100. 0.1 + 0.2. Float infinity. Float nan }.
values do: [:v | out print: v] separatedBy: [out space].
finite := values copyFrom: 1 to: 13.
out space; print: finite = (finite collect: [:v | v printString asNumber]).
out contents
