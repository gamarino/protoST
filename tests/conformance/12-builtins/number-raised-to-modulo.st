"EXPECT: 0 failed of 24 checked"
"R3: raisedTo:modulo: is Python's pow(base, exponent, modulus) on Integers:
 the result takes the modulus's sign, a modulus of 1 or -1 gives 0, and a
 negative exponent raises the inverse of the base. Every expected value is
 what python3.14 answers; the moduli cover machine words, 2^61 - 1, 2^63 - 25
 and moduli of 89 and 127 bits. Errors: a base with no inverse signals
 ArithmeticError, a zero modulus ZeroDivide, a non-Integer operand Error."
| p rows failed raises |
p := (2 raisedTo: 61) - 1.
rows := {
  {3. -1. 7. 5}. {2. 10. 1000. 24}. {-2. 3. 5. 2}. {2. 3. -5. -2}. {5. 0. 1. 0}.
  {7. -2. 10. 9}. {-3. -1. 7. 2}. {4. -1. -7. -5}. {2. -1. 1. 0}. {0. -1. 1. 0}.
  {5. 0. -7. -6}. {-7. -1. 10. 7}.
  {(2 raisedTo: 100) + 1. 2 raisedTo: 70. (10 raisedTo: 20) + 39. 37402239651650905462}.
  {10 raisedTo: 30. 10 raisedTo: 25. ((10 raisedTo: 40) + 3) negated. -7739276815519830795508974164167089192596}.
  {10. -1. p. 2075258708292324556}.
  {12345678901234567890. 98765. p. 240380114002407894}.
  {-5. (2 raisedTo: 64) + 1. (2 raisedTo: 127) - 1. 47041804226768350447710862780734398538}.
  {3. -5. (2 raisedTo: 89) - 1. 486515529842608297336898614}.
  {2 raisedTo: 64. -1. (2 raisedTo: 127) - 1. 9223372036854775808}.
  {6. (2 raisedTo: 62) + 3. (2 raisedTo: 63) - 25. 9223369215744868327} }.
failed := OrderedCollection new.
rows do: [:r | | got |
  got := (r at: 1) raisedTo: (r at: 2) modulo: (r at: 3).
  got = (r at: 4) ifFalse: [failed add: r printString , ' -> ' , got printString]].
raises := [:blk :cls | [blk value. false] on: cls do: [:e | e return: true]].
(raises value: [4 raisedTo: -1 modulo: 8] value: ArithmeticError) ifFalse: [failed add: 'not invertible'].
(raises value: [2 raisedTo: 3 modulo: 0] value: ZeroDivide) ifFalse: [failed add: 'zero modulus'].
(raises value: [2.0 raisedTo: 3 modulo: 5] value: Error) ifFalse: [failed add: 'Float base'].
(raises value: [2 raisedTo: 3 modulo: (1/2)] value: Error) ifFalse: [failed add: 'Fraction modulus'].
failed isEmpty
  ifTrue: [failed size printString , ' failed of ' , (rows size + 4) printString , ' checked']
  ifFalse: [failed asArray printString]
