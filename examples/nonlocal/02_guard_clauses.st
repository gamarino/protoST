"EXPECT: 120"
"-- 02_guard_clauses.st ----------------------------------------------------"
"-- A `^` written directly in a method body is an ordinary return; used     --"
"-- early it forms a guard clause. This recursive factorial returns 1       --"
"-- immediately for the base case, then recurses for the general case.      --"
"-- Expected output: 120  (5 factorial)                                     --"

Object subclass: #MathOps.

MathOps >> factorial: n
  n <= 1 ifTrue: [ ^ 1 ].
  ^ n * (self factorial: n - 1).

MathOps new factorial: 5.
