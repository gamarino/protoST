"EXPECT: 0 failed of 55 checked"
"R3: hash of a number is CPython's numeric hash (modulo P = 2^61 - 1, Python's
 'Hashing of numeric types'), for SmallInteger, LargeInteger, Float, Fraction
 and Decimal. Every expected value below is what python3.14 answers for
 hash() of the same number (Fraction and Decimal from Python's fractions and
 decimal modules): -1 hashes to -2, 2^61 - 1 to 0, the infinities to
 +-314159, and a Fraction whose denominator is a multiple of P to +-314159."
| m D d p rows failed |
m := Import from: 'decimal'.
D := m Decimal.
d := [:t | D fromString: t].
p := (2 raisedTo: 61) - 1.
rows := {
  {0. 0}. {1. 1}. {-1. -2}. {-2. -2}.
  {2 raisedTo: 60. 1152921504606846976}. {p - 1. 2305843009213693950}. {p. 0}.
  {2 raisedTo: 61. 1}. {(2 raisedTo: 61) + 1. 2}. {(2 raisedTo: 61) negated. -2}. {p negated. 0}.
  {2 raisedTo: 64. 8}. {(2 raisedTo: 64) negated. -8}. {(2 raisedTo: 63) negated. -4}.
  {10 raisedTo: 30. 465258685558744706}. {(10 raisedTo: 30) negated. -465258685558744706}.
  {9007199254740993. 9007199254740993}. {-9007199254740992. -9007199254740992}.
  {0.5. 1152921504606846976}. {-0.5. -1152921504606846976}. {1.5. 1152921504606846977}.
  {-1.5. -1152921504606846977}. {0.1. 230584300921369408}. {1.0e100. 1822893315824342674}.
  {5e-324. 16777216}. {1.0e-320. 33957085184}. {1.7976931348623157e308. 2234066890152476671}.
  {-0.0. 0}. {(2 raisedTo: 64) asFloat. 8}. {9007199254740993.0. 9007199254740992}.
  {Float infinity. 314159}. {Float infinity negated. -314159}.
  {1/3. 1537228672809129301}. {-1/3. -1537228672809129301}. {2/7. 1647030720866924251}.
  {1/2. 1152921504606846976}. {1 / p. 314159}. {-1 / p. -314159}.
  {d value: '1'. 1}. {d value: '1.00'. 1}. {d value: '0.5'. 1152921504606846976}.
  {d value: '-0.5'. -1152921504606846976}. {d value: '0.1'. 2075258708292324556}.
  {d value: '1E+30'. 465258685558744706}. {d value: '1E-30'. 1318993825257351686}.
  {d value: '-1'. -2}. {d value: 'Infinity'. 314159}. {d value: '-Infinity'. -314159}.
  {d value: '123.456E-7'. 730935572821493608}. {d value: '1E+999999999'. 527310112413962122}.
  {d value: '-0'. 0}. {d value: '18446744073709551616'. 8}.
  {(1/3) asFloat. 768614336404564608}. {d value: '0'. 0}. {d value: '-0.000'. 0} }.
failed := OrderedCollection new.
rows do: [:r |
  r first hash = r last ifFalse: [
    failed add: r first printString , ' hash = ' , r first hash printString , ', not ' , r last printString]].
failed isEmpty
  ifTrue: [failed size printString , ' failed of ' , rows size printString , ' checked']
  ifFalse: [failed asArray printString]
