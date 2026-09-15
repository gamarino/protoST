"EXPECT: 2"
"§6.1 + D29: a to:do: body may create a block it never evaluates. The block"
"does not run, and the loop still runs once per integer."
n := 0.
1 to: 2 do: [ :i | [ :e | n := 100 ]. n := n + 1 ].
n.
