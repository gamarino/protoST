"EXPECT: #(1 1 3 true true 2)"
"R3, D52: a class object is a key like any other object. Its hash is
 Object's identity hash, reached through the metaclass chain, never its
 instances' hash: Fraction >> hash reads numerator and denominator, which a
 class does not have, and Point >> hash reads x and y."
| s |
s := Set new.
s add: Fraction; add: Point; add: 1/2.
{ (Set with: Fraction) size.
  (Set with: Fraction with: Fraction) size.
  s size.
  s includes: Fraction.
  (Dictionary new at: Point put: 1; yourself) includesKey: Point.
  (Set with: Fraction with: Fraction class) size }
