"EXPECT: #(false true false true true true 1 1 (1/10) false #(-1 -2 -3 1 2) nil 3 (1/4) -3 1500.0)"
"Fractions compare with Floats exactly, as in Pharo ((1/10) = 0.1 is false,
 (1/2) = 0.5 is true), equal numbers have equal hashes in Sets and
 Dictionaries, halves round away from zero, and a few conversions follow
 Pharo."
{ (1/10) = 0.1. (1/2) = 0.5. 0.1 = (1/10). 0.5 = (1/2).
  (1/2) hash = 0.5 hash. (1/3) < (1/3) asFloat or: [(1/3) > (1/3) asFloat].
  (Set with: 0.5 with: (1/2)) size. (Set with: (1/2)) size.
  0.1 asExactFraction - (3602879701896397/36028797018963968) + (1/10).
  ((Set with: 0.5) includes: (1/2)) not.
  {(-1/2) rounded. (-3/2) rounded. (-5/2) rounded. (1/2) rounded. (3/2) rounded}.
  '0x10' asNumber. '3.' asNumber. 2 raisedTo: -2. -7.5 quo: 2. '1.5e3' asNumber }
