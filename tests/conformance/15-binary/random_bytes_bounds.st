"EXPECT: 3 refused"
"ByteArray randomBytes: takes a SmallInteger from 0 to 1048576 (1 MiB);
 a negative size, a size above the bound and a non-Integer each signal
 InvalidArgument."
| n |
n := 0.
{-1. 1048577. 'x'} do: [:arg |
  [ByteArray randomBytes: arg] on: InvalidArgument do: [:e | n := n + 1]].
n printString , ' refused'.
