"EXPECT: true"
"§2.9: a bare identifier inside #( ) is treated as a symbol"
(#(foo bar) at: 1) = #foo.
