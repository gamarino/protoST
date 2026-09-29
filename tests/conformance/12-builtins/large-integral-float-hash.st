"EXPECT: #(true true true)"
"An integral Float and the equal Integer hash alike, also above 2^53, so
 hashed collections find one through the other."
{ (Set with: (2 raisedTo: 60)) includes: (2 raisedTo: 60) asFloat.
  9007199254740991 hash = 9007199254740991.0 hash.
  (Dictionary new at: (2 raisedTo: 70) put: #x; yourself) includesKey: (2 raisedTo: 70) asFloat }
