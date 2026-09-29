"EXPECT: 100 200 300"
"Blocks created by collect: keep the element of their own iteration."
bs := #(1 2 3) collect: [:x | [x * 100]].
((bs at: 1) value) printString , ' ' , ((bs at: 2) value) printString , ' ' , ((bs at: 3) value) printString.
