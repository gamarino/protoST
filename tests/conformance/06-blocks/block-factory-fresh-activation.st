"EXPECT: 11 21"
"Each activation of a block factory has its own parameter binding."
mkAdder := [:k | [:x | x + k]].
p := mkAdder value: 10.
q := mkAdder value: 20.
(p value: 1) printString , ' ' , (q value: 1) printString.
