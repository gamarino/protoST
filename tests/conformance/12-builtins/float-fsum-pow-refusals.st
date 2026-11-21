"EXPECT: fsumOfPowSquaredDifferences:with: the sequences differ in size (2 and 3)|fsumOfPowSquaredDifferences:with: the sequences differ in size (1 and 0)|fsumOfPowSquaredDifferences:with: element 2 of the second sequence is not a Float|fsumOfPowSquaredDifferences:with: element 1 of the first sequence is not a Float|fsumOfPowSquaredDifferences:with: element 1 of the first sequence is not a Float|fsumOfPowSquaredDifferences:with: element 2 of the second sequence is not a Float|fsumOfPowSquaredDifferences:with: the argument does not answer size and at: as a sequence|fsumOfPowSquaredDifferences:with: the argument does not answer size and at: as a sequence|fsumOfPowSquaredDifferences:with: element 1 of the first sequence is not a Float|fsum: intermediate overflow|0.0|14.0|25.0|true|true"
"Float fsumOfPowSquaredDifferences:with: accepts and refuses what fsumOfSquaredDifferences:with: does, with its own selector in the texts: two sequences of different sizes, an element that is not a Float (an Integer, a Fraction, nil), an argument that does not answer size and at:. The sum errors are fsum's. An Interval of Floats and an OrderedCollection are read; empty sequences answer 0.0. The errors are Errors; a square that overflows is infinity, as with raisedTo: 2."
Object subclass: #NotASequence instanceVariableNames: ''.

| texts try |
texts := OrderedCollection new.
try := [:block | texts add: ([block value printString] on: Error do: [:e | e messageText])].
try value: [Float fsumOfPowSquaredDifferences: #(1.0 2.0) with: #(1.0 2.0 3.0)].
try value: [Float fsumOfPowSquaredDifferences: #(1.0) with: #()].
try value: [Float fsumOfPowSquaredDifferences: #(1.0 2.0) with: #(1.0 2)].
try value: [Float fsumOfPowSquaredDifferences: { 1/2 } with: #(1.0)].
try value: [Float fsumOfPowSquaredDifferences: { nil. 2.0 } with: #(1.0 2.0)].
try value: [Float fsumOfPowSquaredDifferences: #(1.0 2.0) with: (OrderedCollection with: 1.0 with: 'x')].
try value: [Float fsumOfPowSquaredDifferences: NotASequence new with: #(1.0)].
try value: [Float fsumOfPowSquaredDifferences: #(1.0) with: 3.0].
try value: [Float fsumOfPowSquaredDifferences: (1 to: 3) with: #(1.0 2.0 3.0)].
try value: [Float fsumOfPowSquaredDifferences: #(1.0e154 1.0e154) with: #(0.0 0.0)].
try value: [Float fsumOfPowSquaredDifferences: #() with: #()].
try value: [Float fsumOfPowSquaredDifferences: (1.0 to: 3.0) with: #(0.0 0.0 0.0)].
try value: [Float fsumOfPowSquaredDifferences: (OrderedCollection with: 4.0 with: 0.0) with: #(1.0 -4.0)].
texts add: ([Float fsumOfPowSquaredDifferences: #(1.0e154 1.0e154) with: #(0.0 0.0)] on: Error do: [:e | e class == Error]) printString.
texts add: ((Float fsumOfPowSquaredDifferences: { 1.0e200 } with: #(0.0)) = Float infinity) printString.
texts inject: '' into: [:acc :t | acc isEmpty ifTrue: [t] ifFalse: [acc , '|' , t]].
