"EXPECT: 0 mismatches in 36000 comparisons"
"A randomized cross-check of the Fraction comparisons against the
 reference they replaced: the order of a and b is the sign of the exact
 difference a - b (the kernel's former <, which subtracted and read the
 numerator's sign), with a finite Float taken as its exact value
 (asExactFraction, as a Float receiver always did). Every pair is compared
 with < <= > >= = ~= in both directions. Operands are Fractions, Integers
 and LargeIntegers of every magnitude class (small, near the SmallInteger
 boundary of 2^53, far beyond it), of either sign, and finite Floats,
 some of them the nearest Float to a Fraction of the set. The generator is
 a fixed linear congruential sequence, so the run is reproducible."
Object subclass: #CrossCheck instanceVariableNames: 'seed'.

CrossCheck >> seed: anInteger
  seed := anInteger.

CrossCheck >> next: bound
  "An Integer from 0 to bound - 1 (a 61-bit LCG, high bits first)."
  seed := (seed * 6364136223846793005 + 1442695040888963407) \\ (2 raisedTo: 64).
  ^ (seed bitShift: -20) \\ bound.

CrossCheck >> integer
  | magnitude v |
  magnitude := #(7 100 1000000 9007199254740991 9007199254740993 18446744073709551616 1267650600228229401496703205376)
    at: (self next: 7) + 1.
  v := (self next: magnitude) + 1.
  (self next: 5) = 0 ifTrue: [v := magnitude + (self next: 3) - 1].
  ^ (self next: 2) = 0 ifTrue: [v negated] ifFalse: [v].

CrossCheck >> operand
  | kind |
  kind := self next: 10.
  kind < 2 ifTrue: [^ self integer].
  kind < 3 ifTrue: [^ (self integer / self integer abs) asFloat].
  kind < 4 ifTrue: [^ (self next: 2000) - 1000 / 8.0].
  ^ self integer / self integer abs.

CrossCheck >> exact: aNumber
  ^ aNumber isFloat ifTrue: [aNumber asExactFraction] ifFalse: [aNumber].

CrossCheck >> expected: op sign: s
  op == #< ifTrue: [^ s < 0].
  op == #<= ifTrue: [^ s <= 0].
  op == #> ifTrue: [^ s > 0].
  op == #>= ifTrue: [^ s >= 0].
  op == #= ifTrue: [^ s = 0].
  ^ s ~= 0.

CrossCheck >> run: pairs
  | bad count ops a b s |
  bad := 0. count := 0.
  ops := #(#< #<= #> #>= #= #~=).
  pairs timesRepeat: [
    a := self operand.
    b := self operand.
    "At least one side is a Fraction: Integer and Float pairs are native."
    (a isFraction or: [b isFraction]) ifFalse: [a := (self exact: a) + (1/3)].
    s := ((self exact: a) - (self exact: b)) sign.
    ops do: [:op |
      count := count + 2.
      (a perform: op with: b) = (self expected: op sign: s) ifFalse: [
        bad := bad + 1.
        (a printString , ' ' , op , ' ' , b printString) displayNl].
      (b perform: op with: a) = (self expected: op sign: s negated) ifFalse: [
        bad := bad + 1.
        (b printString , ' ' , op , ' ' , a printString) displayNl]]].
  ^ bad printString , ' mismatches in ' , count printString , ' comparisons'.

(CrossCheck new seed: 20261119; yourself) run: 3000.
