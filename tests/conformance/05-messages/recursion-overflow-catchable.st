"EXPECT: caught"
"Unbounded recursion raises an Error that on:do: catches, even at top level."
Object subclass: #R.
R >> forever: n
  ^ self forever: n + 1.

[R new forever: 0] on: Error do: [:e | 'caught'].
