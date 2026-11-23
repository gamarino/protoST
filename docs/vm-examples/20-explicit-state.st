Object subclass: #Box
  instanceVariableNames: 'a b'
  constantNames: 'k'
  writeOnceNames: 'w'.

Box >> initialize
  a := 1. b := 2. k := 3. a := 4. w := 5.

Box >> k
  ^ k.

Box new k printNl.
