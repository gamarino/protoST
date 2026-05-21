"EXPECT: true"
"§13 stdlib: Time millisecondsToRun: returns a non-negative integer — the
monotonic clock never runs backwards."
m := Import from: 'time'.
elapsed := m Time millisecondsToRun: [ | s | s := 0. 1 to: 1000 do: [ :i | s := s + i ] ].
elapsed >= 0.
