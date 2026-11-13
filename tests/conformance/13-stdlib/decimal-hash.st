"EXPECT: true true true true true true true true true"
"§13 stdlib: Decimal hash agrees with = across classes: an equal Integer,
 LargeInteger, Fraction or Float has the same hash (1.00 / 1 / 1.0;
 -0 / 0 / 0.0; 0.5 / 1/2 / 0.5; 1.25 / 5/4; 1E+30 / 10^30; 1E+22 / 1e22,
 a Float that is exactly 10^22; -2.50 / -5/2; 123.4500 / 24690/200; and
 two Decimals of one value, 1.0E+3 / 1000)."
| m D d same |
m := Import from: 'decimal'.
D := m Decimal.
d := [:t | D fromString: t].
same := [:a :b | a = b and: [b = a and: [a hash = b hash]]].
{ (same value: (d value: '1.00') value: 1) and: [same value: (d value: '1.00') value: 1.0].
  (same value: (d value: '-0') value: 0) and: [same value: (d value: '-0') value: 0.0].
  (same value: (d value: '0.5') value: (1/2)) and: [same value: (d value: '0.5') value: 0.5].
  same value: (d value: '1.25') value: (5/4).
  same value: (d value: '1E+30') value: (10 raisedTo: 30).
  same value: (d value: '1E+22') value: 1.0e22.
  same value: (d value: '-2.50') value: (-5/2).
  same value: (d value: '123.4500') value: (24690/200).
  same value: (d value: '1.0E+3') value: (d value: '1000') }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each printString] ifFalse: [acc , ' ' , each printString]].
