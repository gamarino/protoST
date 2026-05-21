"EXPECT: true"
"-- 05_time.st ------------------------------------------------------------"
"-- The `time` standard-library module. `Time millisecondsToRun:` times a   --"
"-- block on the monotonic clock; `Duration` models a millisecond span      --"
"-- with arithmetic. Here we benchmark a loop and confirm the elapsed time  --"
"-- is a sane non-negative value below a generous Duration bound.           --"
"-- Expected output: true                                                   --"

m := Import from: 'time'.

elapsed := m Time millisecondsToRun: [
  | s |
  s := 0.
  1 to: 100000 do: [ :i | s := s + i ] ].

"elapsed is an integer count of milliseconds; compare against a Duration."
budget := m Duration seconds: 60.
(elapsed >= 0) and: [ elapsed <= budget asMilliseconds ].
