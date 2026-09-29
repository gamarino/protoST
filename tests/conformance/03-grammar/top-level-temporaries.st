"EXPECT: 7"
"Workspace-style temporaries at the top of a file are accepted and start nil."
| a b |
a printNl.
a := 3.
b := [:x | x + a] value: 4.
b printNl.
