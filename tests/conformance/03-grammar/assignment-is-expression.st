"XFAIL: EXPECT: 0"
"§3.6: the spec states 'a := b := 0 is legal' — an assignment is itself an
expression. NEW — undocumented discrepancy (not in §14): the parser rejects a
chained assignment with 'expected primary expression, got Assign'. See report."
a := b := 0.
a.
