"XFAIL: EXPECT: 2"
"§2.9 + D16: nested #( ) inside #( ) is not parsed. Spec-correct: a nested
literal array should be a valid element, so #(#(1 2) #(3 4)) has size 2."
#(#(1 2) #(3 4)) size.
