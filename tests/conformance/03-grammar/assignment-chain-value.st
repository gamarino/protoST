"XFAIL: EXPECT: 5"
"§3.6: the value of an assignment expression is the assigned value, so a chained
assignment a := b := 5 should bind both. NEW — undocumented discrepancy (not in
§14): the parser rejects chained assignment. See report."
a := b := 5.
b.
