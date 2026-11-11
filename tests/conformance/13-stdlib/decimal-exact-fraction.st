"EXPECT: (5/4) 0 1000 (-1/8) (6/5) true refused refused"
"§13 stdlib: Decimal asExactFraction answers an Integer or a reduced
 Fraction, for finite values only; Infinity and NaN signal Error."
| m D refuse r |
m := Import from: 'decimal'.
D := m Decimal.
refuse := [:blk | [blk value. 'accepted'] on: Error do: [:e | 'refused']].
r := OrderedCollection new.
#('1.25' '-0' '1E+3' '-0.125' '1.20') do: [:t | r add: (D fromString: t) asExactFraction printString].
r add: (D fromString: '1E+3') asExactFraction isInteger printString.
r add: (refuse value: [(D fromString: 'Infinity') asExactFraction]).
r add: (refuse value: [(D fromString: 'NaN') asExactFraction]).
(r inject: '' into: [:acc :each | acc isEmpty ifTrue: [each] ifFalse: [acc , ' ' , each]]).
