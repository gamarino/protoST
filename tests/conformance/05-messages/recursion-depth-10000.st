"EXPECT: 10000"
"Recursion of 10 000 frames works (0.4.0; it failed at 145 before)."
Object subclass: #R.
R >> down: n
  n = 0 ifTrue: [^ 0].
  ^ 1 + (self down: n - 1).

R new down: 10000.
