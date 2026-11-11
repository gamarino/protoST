"EXPECT: true true true true true true true true true true true true true true true true | refused refused refused refused refused refused refused | 1 2 true true"
"§13 stdlib: comparison with a number on the left is symmetric, as in
 Python and ProtoBase (1 == 1.0 == Decimal(1)): the numeric primitives and
 Fraction dispatch back to the Decimal through adaptToNumber:andSend:.
 Integer, LargeInteger, Float and Fraction receivers; =, ~=, <, <=, >, >=.
 Second group: arithmetic with a number stays refused (D49), from either
 side, and ordering against a NaN signals. Third group: hashed collections
 agree both ways (1, 1.0 and 1.00 are one element; 1/2 and 0.50 another)."
| m D d r refuse s |
m := Import from: 'decimal'.
D := m Decimal.
d := [:t | D fromString: t].
refuse := [:blk | [blk value. 'accepted'] on: Error do: [:e | 'refused']].
r := OrderedCollection new.
r add: (1 = (d value: '1.00')) printString.
r add: (1.0 = (d value: '1')) printString.
r add: ((5/4) = (d value: '1.25')) printString.
r add: ((10 raisedTo: 30) = (d value: '1E+30')) printString.
r add: (0.5 = (d value: '0.500')) printString.
r add: (0.1 = (d value: '0.1')) not printString.
r add: (1 ~= (d value: '1.0')) not printString.
r add: ((5/4) ~= (d value: '1.3')) printString.
r add: (1 < (d value: '1.01')) printString.
r add: ((10 raisedTo: 30) > (d value: '9.99E+29')) printString.
r add: (1.5 <= (d value: '1.50')) printString.
r add: ((1/2) < (d value: '0.6')) printString.
r add: ((1/2) > (d value: '0.4')) printString.
r add: (((1/2) <= (d value: '0.5')) and: [(1/2) >= (d value: '0.5')]) printString.
r add: ((Float infinity = (d value: 'Infinity')) and: [-1 > (d value: '-Infinity')]) printString.
r add: ((1 = (d value: 'NaN')) not and: [(1 ~= (d value: 'NaN')) and: [((1/2) = (d value: 'NaN')) not]]) printString.
r add: '|'.
r add: (refuse value: [1 + (d value: '2')]).
r add: (refuse value: [1.5 * (d value: '2')]).
r add: (refuse value: [(1/2) + (d value: '2')]).
r add: (refuse value: [(1/2) - (d value: '2')]).
r add: (refuse value: [1 / (d value: '2')]).
r add: (refuse value: [1 < (d value: 'NaN')]).
r add: (refuse value: [(1/2) >= (d value: 'sNaN')]).
r add: '|'.
s := Set new.
s add: 1; add: 1.0; add: (d value: '1.00').
r add: s size printString.
s := Set new.
s add: (d value: '0.50'); add: (1/2); add: 0.5; add: (d value: '2'); add: 2.
r add: s size printString.
r add: ((Set new add: 1; yourself) includes: (d value: '1.0')) printString.
r add: ((Set new add: (d value: '1.0'); yourself) includes: 1) printString.
(r inject: '' into: [:acc :each | acc isEmpty ifTrue: [each] ifFalse: [acc , ' ' , each]]).
