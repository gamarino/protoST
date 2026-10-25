Object subclass: #Math.

Math >> fib: n
  n < 2 ifTrue: [ ^ n ].
  ^ (self fib: n - 1) + (self fib: n - 2).

(Math new fib: 10) printNl.
