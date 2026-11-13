"EXPECT: 1 1048576 ByteArray false 2 refused"
"ByteArray randomBytes: at the edges: one byte, the bound itself (1048576),
 the class of the answer, a 4096-byte draw that is not all zero (probability
 2^-32768 of failing), and two more refusals: a LargeInteger and nil."
| n |
n := 0.
{2 raisedTo: 100. nil} do: [:arg |
  [ByteArray randomBytes: arg] on: InvalidArgument do: [:e | n := n + 1]].
(ByteArray randomBytes: 1) size printString , ' ' ,
  (ByteArray randomBytes: 1048576) size printString , ' ' ,
  (ByteArray randomBytes: 8) class name , ' ' ,
  ((ByteArray randomBytes: 4096) = (ByteArray new: 4096)) printString , ' ' ,
  n printString , ' refused'.
