"EXPECT: #(true true true true true true 1 1 1 true true)"
"R3: equal numbers of different kinds hash alike, so the hashed collections
 hold them as one key: 1, 1.0, Decimal 1 and Decimal 1.00; 0.5, 1/2 and
 Decimal 0.5; 2^64 as a LargeInteger, as a Float and as a Decimal; -1 and
 -1.0 and Decimal -1 (all -2, as in Python); a large negative integer and its
 Float. A NaN equals nothing and hashes by identity: the same NaN hashes
 alike. A signaling Decimal NaN cannot be hashed (Python's TypeError)."
| m D d alike nan |
m := Import from: 'decimal'.
D := m Decimal.
d := [:t | D fromString: t].
alike := [:values | values allSatisfy: [:v |
  (v = values first) and: [v hash = values first hash]]].
nan := Float nan.
{ alike value: {1. 1.0. d value: '1'. d value: '1.00'. 2/2}.
  alike value: {0.5. 1/2. d value: '0.5'. d value: '0.50000'}.
  alike value: {2 raisedTo: 64. (2 raisedTo: 64) asFloat. d value: '1.8446744073709551616E+19'}.
  alike value: {-1. -1.0. d value: '-1'. d value: '-1.0'}.
  alike value: {(2 raisedTo: 80) negated. (2 raisedTo: 80) negated asFloat}.
  nan hash = nan hash.
  (Set new add: 1; add: 1.0; add: (d value: '1.00'); yourself) size.
  (Set new add: 0.5; add: 1/2; add: (d value: '0.5'); yourself) size.
  (Dictionary new at: (2 raisedTo: 64) put: #a; at: (2 raisedTo: 64) asFloat put: #b; yourself) size.
  (Dictionary new at: (1/2) put: #half; yourself) includesKey: 0.5.
  [(d value: 'sNaN') hash. false] on: Error do: [:e | e return: true] }
