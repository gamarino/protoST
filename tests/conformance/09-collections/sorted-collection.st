"EXPECT: #(1 2 5 9) SortedCollection #('ccc' 'bb' 'a') #(1 2 3)"
"A SortedCollection keeps its order as elements are added; sorted and sort:
 take a sort block."
sc := SortedCollection new.
sc add: 5; add: 1; add: 9; add: 2.
sc asArray printString , ' ' , sc class name , ' ' , (#('bb' 'a' 'ccc') sorted: [:x :y | x size >= y size]) printString , ' ' , #(3 1 2) sorted printString.
