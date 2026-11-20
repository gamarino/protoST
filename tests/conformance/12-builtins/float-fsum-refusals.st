"EXPECT: fsum: element 2 is not a Float|fsum: element 1 is not a Float|fsum: element 3 is not a Float|fsum: element 2 is not a Float|fsumOfProducts:with: the sequences differ in size (2 and 3)|fsumOfSquaredDifferences:with: the sequences differ in size (1 and 0)|fsumOfProducts:with: element 2 of the second sequence is not a Float|fsumOfSquaredDifferences:with: element 1 of the first sequence is not a Float|fsum: the argument does not answer size and at: as a sequence|fsum: the argument does not answer size and at: as a sequence|fsum: element 1 is not a Float|0.0|6.0|Float nan|true"
"Float fsum: and its two-sequence forms take Floats only: an Integer, a Fraction, nil or any other element is refused (protoBaseST converts with floatOf: first). Two sequences of different sizes are refused. An argument that does not answer size and at: is refused. A sequence of Floats that is neither an Array nor an OrderedCollection is read through size and at:, an Interval of Floats included."
Object subclass: #NotASequence instanceVariableNames: ''.

| texts try |
texts := OrderedCollection new.
try := [:block | texts add: ([block value printString] on: Error do: [:e | e messageText])].
try value: [Float fsum: #(1.0 2 3.0)].
try value: [Float fsum: { 1/2. 1.0 }].
try value: [Float fsum: { 1.0. 2.0. nil }].
try value: [Float fsum: (OrderedCollection with: 1.0 with: 'x')].
try value: [Float fsumOfProducts: #(1.0 2.0) with: #(1.0 2.0 3.0)].
try value: [Float fsumOfSquaredDifferences: #(1.0) with: #()].
try value: [Float fsumOfProducts: #(1.0 2.0) with: #(1.0 2)].
try value: [Float fsumOfSquaredDifferences: #(1) with: #(1.0)].
try value: [Float fsum: NotASequence new].
try value: [Float fsum: 3.0].
try value: [Float fsum: (1 to: 3)].
try value: [Float fsum: #()].
try value: [Float fsum: (1.0 to: 3.0)].
try value: [Float fsum: { Float nan }].
texts add: ([Float fsum: #(1.0e308 1.0e308)] on: Error do: [:e | e class == Error]) printString.
texts inject: '' into: [:acc :t | acc isEmpty ifTrue: [t] ifFalse: [acc , '|' , t]].
