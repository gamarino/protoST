"EXPECT: true true false false true false refused | true true true true true true true true true true false refused refused false true true true"
"§13 stdlib: Decimal compares with any Number through exact fractions.
 1.00 = 1 and = 1.0; 0.1 is not the Float 0.1; 1.0 and 1.00 are equal but
 not the same representation; infinities order at the ends; a NaN is never
 equal and refuses ordering. The second group: -0 = 0, Decimal = Decimal,
 hash agrees with = for Integer, Float and Fraction, comparisons with huge
 exponents (answered without building 10^999999), Infinity = Float infinity,
 a non-Number is unequal and refuses ordering, and the other operators."
| m D d refuse r |
m := Import from: 'decimal'.
D := m Decimal.
d := [:t | D fromString: t].
refuse := [:blk | [blk value. 'accepted'] on: Error do: [:e | 'refused']].
r := OrderedCollection new.
r add: ((d value: '1.00') = 1) printString.
r add: ((d value: '1.00') = 1.0) printString.
r add: ((d value: '0.1') = 0.1) printString.
r add: ((d value: '1.0') sameRepresentationAs: (d value: '1.00')) printString.
r add: ((d value: '-Infinity') < (d value: '-1E+999999')) printString.
r add: ((d value: 'NaN') = (d value: 'NaN')) printString.
r add: (refuse value: [(d value: 'NaN') < 1]).
r add: '|'.
r add: ((d value: '-0') = 0) printString.
r add: ((d value: '1.0') = (d value: '1.00')) printString.
r add: ((d value: '1.00') hash = 1 hash) printString.
r add: (((d value: '0.5') hash = 0.5 hash) and: [(d value: '0.5') = 0.5]) printString.
r add: (((d value: '1.25') hash = (5/4) hash) and: [(d value: '1.25') = (5/4)]) printString.
r add: ((d value: '1E+999999') > (d value: '9E+999998')) printString.
r add: (((d value: '1E-999999') > 0) and: [(d value: '1E-999999') < (1/1000)]) printString.
r add: ((d value: '0.1') < (1/3)) printString.
r add: ((d value: 'Infinity') = Float infinity) printString.
r add: ((d value: '-1.5') <= (d value: '-1.50') and: [(d value: '2') >= 2 and: [(d value: '-1') ~= 1]]) printString.
r add: ((d value: '1') = 'a') printString.
r add: (refuse value: [(d value: '1') < 'a']).
r add: (refuse value: [(d value: 'sNaN') > (d value: '1')]).
r add: ((d value: 'sNaN') = (d value: 'sNaN')) printString.
r add: ((d value: '1.0') sameRepresentationAs: (d value: '1.0')) printString.
r add: (((d value: '-NaN7') sameRepresentationAs: (d value: '-NaN7')) and: [((d value: 'NaN7') sameRepresentationAs: (d value: 'sNaN7')) not]) printString.
r add: ((d value: '1E+2') = 100 and: [((d value: '1E+2') sameRepresentationAs: (d value: '100')) not]) printString.
(r inject: '' into: [:acc :each | acc isEmpty ifTrue: [each] ifFalse: [acc , ' ' , each]]).
