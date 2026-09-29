"EXPECT: #(1 $a 'str' #(2 3)) an OrderedCollection(1 2) a Set(5) #a2345678->1"
"Collections print their elements."
(#(1 $a 'str' #(2 3)) printString) , ' ' , ((OrderedCollection new add: 1; add: 2; yourself) printString) , ' ' , ((Set new add: 5; yourself) printString) , ' ' , ((#a2345678 -> 1) printString).
