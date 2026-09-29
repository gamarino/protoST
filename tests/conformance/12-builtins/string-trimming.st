"EXPECT: [ab  ] [  ab] [ab] [ab] [a b] [] [] [x]"
"trimLeft, trimRight and trimSeparators remove separators (space, tab, CR, LF, FF) from one or both ends."
s := '  ab  '.
ws := (String with: Character tab with: Character lf) , ' a b ' , (String with: Character cr).
t1 := '[' , s trimLeft , '] [' , s trimRight , '] [' , s trimSeparators , '] [' , s trimBoth , ']'.
t2 := '[' , ws trimSeparators , '] [' , '   ' trimSeparators , '] [' , '' trimLeft , '] [' , 'x' trimRight , ']'.
t1 , ' ' , t2.
