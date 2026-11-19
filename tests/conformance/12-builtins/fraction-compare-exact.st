"EXPECT: #(true false true false true true true true true true true true true true true false true false true true false false true true true true true true true true true true true true true true true true true) ftftft ffffft|ttffft ffttft|ffttft ttffft|ftftft ffffft|ttffft ffttft|ffttft ttffft|ftftft ffffft|ttffft ffttft|ffttft ttffft|"
"Fractions compare exactly with Fractions, Integers, LargeIntegers and finite
 Floats, in both directions, and nothing rounds through Float (the
 comparisons were a subtraction and two normalized Fractions; they are now
 one primitive). An unnormalized Fraction equals the normalized one. The
 last string is the table against NaN, +Inf and -Inf, receiver op Float then
 Float op receiver, for < <= > >= = ~=: the NaN columns are what the kernel
 answered before (a Fraction's <= and >= are the negations of > and <); a
 Fraction too large for a Float (10^400/3) is still below +Inf."
| half twoFourths eightHalves big small n m f out ops tinyBelowHalf |
half := 1/2.
twoFourths := Fraction basicNew setNumerator: 2 denominator: 4.
eightHalves := Fraction basicNew setNumerator: 8 denominator: 2.
big := (10 raisedTo: 40) + 1 / ((10 raisedTo: 40) + 3).
small := (10 raisedTo: 40) + 2 / ((10 raisedTo: 40) + 4).
n := 9007199254740991.
m := 9007199254740990.
tinyBelowHalf := (1/2) - (1 / (2 raisedTo: 80)).
f := {
  (1/3) < (1/2). (1/2) < (1/3). (-1/2) < (-1/3). (-1/3) <= (-1/2).
  twoFourths = half. half = twoFourths. twoFourths <= half. twoFourths >= half.
  (twoFourths < half) not. twoFourths hash = half hash.
  eightHalves = 4. 4 = eightHalves.
  (7/2) < 4. (7/2) > 3. (-7/2) < -3. (-7/2) > -3.
  4 > (7/2). 3 >= (7/2). -4 < (-7/2).
  ((2 raisedTo: 100) + 1 / 2) > (2 raisedTo: 99).
  ((2 raisedTo: 100) + 1 / 2) >= ((2 raisedTo: 99) + 1).
  ((2 raisedTo: 100) + 1 / 2) = (2 raisedTo: 99).
  big < small. small > big. (n / m) < (m / (m - 1)). ((n / m) = (n / m)).
  tinyBelowHalf < 0.5. tinyBelowHalf <= 0.5. (tinyBelowHalf >= 0.5) not.
  (tinyBelowHalf = 0.5) not. 0.5 > tinyBelowHalf. (1/2) = 0.5.
  ((1/3) < (1/3) asFloat) not. (1/3) > -0.0. (1 / (2 raisedTo: 1075)) < (0.5 raisedTo: 1074).
  (1 / (2 raisedTo: 1074)) = (0.5 raisedTo: 1074). ((10 raisedTo: 299) / 3) < 1.0e300.
  ((3/2) max: 1) = (3/2). ((1/2) between: 0 and: 1) }.
out := WriteStream on: String new.
ops := #(#< #<= #> #>= #= #~=).
{1/3. (10 raisedTo: 400) / 3. ((10 raisedTo: 400) / 3) negated} do: [:x |
  {Float nan. Float infinity. Float infinity negated} do: [:y |
    ops do: [:op | out nextPutAll: ((x perform: op with: y) printString first: 1)].
    out nextPut: $ .
    ops do: [:op | out nextPutAll: ((y perform: op with: x) printString first: 1)].
    out nextPut: $|]].
f printString , ' ' , out contents.
