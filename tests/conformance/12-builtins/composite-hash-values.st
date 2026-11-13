"EXPECT: #(1152921504608346982 1729382256910520461 384307168202615662 2305843009211693943 1976436865552310637 239431194203410158 1152921504608346982 1152921504607346985 1847106495482653119 617323440330896928 1828646288756888216 0 1152923504619346993)"
"R3, review round 2: the combined hashes of Point, Association and
 SequenceableCollection (and MessagePackMap, which folds its keys the same
 way) are (h * 1000003 + e hash) \\ (2^61 - 1), computed natively by
 __hashMix:. The expected values are Python's, from hash() of each element:
 P = 2**61 - 1; mix(a, b) = (hash(a) * 1000003 + hash(b)) % P; a sequence
 starts at its size and mixes its first 8 elements."
{ (1.5 @ 2) hash.
  (0.25 @ -3) hash.
  ((1/3) @ 2.5) hash.
  (-1 @ -1) hash.
  ((2 raisedTo: 70) @ (1/7)) hash.
  (1.0e300 @ -0.5) hash.
  (1.5 -> 2) hash.
  ((1/2) -> (2 raisedTo: 64)) hash.
  #(1 2.5 -3) hash.
  (Array with: 1/3 with: 0.1 with: (2 raisedTo: 100)) hash.
  #(1 2 3 4 5 6 7 8 9 10) hash.
  #() hash.
  (OrderedCollection new add: 0.5; add: -2; yourself) hash }
