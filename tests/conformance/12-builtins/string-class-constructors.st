"EXPECT: 13 10 9 [ ] 2 zzz 0 true '' true 3"
"String cr / lf / tab / space / crlf, new:withAll:, new (an empty String) and new: (a String of n spaces)."
t1 := String cr first value printString , ' ' , String lf first value printString , ' ' , String tab first value printString , ' [' , String space , '] ' , String crlf size printString.
t2 := (String new: 3 withAll: $z) , ' ' , String new size printString , ' ' , String new isString printString , ' ' , String new printString , ' ' , (String new = '') printString , ' ' , (String new: 3) size printString.
t1 , ' ' , t2.
