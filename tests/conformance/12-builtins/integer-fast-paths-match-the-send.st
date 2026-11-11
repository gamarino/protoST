"EXPECT: #(true -1 1 -4 -4 -1 9007199254740992 'LargeInteger')"
"*, \\, //, rem:, bitAnd: and bitOr: have SmallInteger fast paths in the engine (0.10.0); each answers what the send answers, for SmallIntegers near the range limits, LargeIntegers, Floats, Fractions and a zero divisor (the error class). The sent form goes through perform:."
| vals bad try |
vals := { 0. 1. -1. 7. -7. 3. -3. 1000000007. -1000000007. 9007199254740991. -9007199254740992. 4611686018427387904. 2.5. 3/4 }.
bad := OrderedCollection new.
try := [ :blk | [ blk value ] on: Error do: [ :e | e class name ] ].
vals do: [ :a | vals do: [ :b |
  { { 'mul'. try value: [ a * b ]. try value: [ a perform: '*' asSymbol with: b ] }.
    { 'mod'. try value: [ a \\ b ]. try value: [ a perform: '\\' asSymbol with: b ] }.
    { 'quo'. try value: [ a // b ]. try value: [ a perform: '//' asSymbol with: b ] }.
    { 'rem'. try value: [ a rem: b ]. try value: [ a perform: 'rem:' asSymbol with: b ] }.
    { 'and'. try value: [ a bitAnd: b ]. try value: [ a perform: 'bitAnd:' asSymbol with: b ] }.
    { 'or'. try value: [ a bitOr: b ]. try value: [ a perform: 'bitOr:' asSymbol with: b ] } }
  do: [ :row | ((row at: 2) = (row at: 3) and: [ (row at: 2) class == (row at: 3) class ])
      ifFalse: [ bad add: { a. b. row } ] ] ] ].
bad isEmpty ifFalse: [ bad printNl ].
{ bad isEmpty. 7 \\ -2. -7 \\ 2. -7 // 2. 7 // -2. -7 rem: 2. 4503599627370496 * 2. (4503599627370496 * 2) class name }
