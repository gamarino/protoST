"EXPECT-ERROR: stack depth exceeded"
"Uncaught, the error message names stack depth."
Object subclass: #R.
R >> forever: n
  ^ self forever: n + 1.

R new forever: 0.
