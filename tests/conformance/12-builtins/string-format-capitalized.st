"EXPECT: [a and b] [33] [5!] [x=1.5 y=#(1)] [{1}] [Hello] [HELLO] [] [1abc]"
"format: interpolates {n} from a sequence (or {key} from a Dictionary) using asString, with \ escaping; capitalized uppercases the first character only."
d := Dictionary new.
d at: 'x' put: 5.
t1 := '[' , ('{1} and {2}' format: #(a b)) , '] [' , ('{1}{1}' format: #(3)) , '] [' , ('{x}!' format: d) , '] [' , ('x={1} y={2}' format: {1.5. #(1)}) , '] [' , ('\{1}' format: #(9)) , ']'.
t2 := '[' , 'hello' capitalized , '] [' , 'hELLO' capitalized , '] [' , '' capitalized , '] [' , '1abc' capitalized , ']'.
t1 , ' ' , t2.
