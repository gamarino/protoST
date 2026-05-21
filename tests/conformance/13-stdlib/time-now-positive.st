"EXPECT: true"
"§13 stdlib: Time now asMilliseconds is a positive integer (the wall clock
reads a sensible epoch-millisecond value)."
m := Import from: 'time'.
(m Time now asMilliseconds) > 0.
