Object subclass: #Foo instanceVariableNames: 'a'.
Foo subclass: #Bar instanceVariableNames: 'b'.
| n c |
n := 0.
1 to: 1000000 do: [:i | c := Bar new. n := n + 1].
n = 1000000 ifFalse: [Error signal: 'wrong count'].
Transcript show: 'VERIFIED ' , n printString; cr.
