"EXPECT: true"
"§13 stdlib: a Timestamp taken after another is never earlier than it — the
wall clock does not run backwards between two ordinary reads."
m := Import from: 'time'.
t1 := m Time now.
t2 := m Time now.
t2 >= t1.
