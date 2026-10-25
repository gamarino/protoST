| pair a b |
pair := { 3 + 4. 'seven' }.
a := pair at: 1.
b := pair at: 2.
#(1 $a 'str' #sym 2.5) size printNl.
(a printString, ' ', b) printNl.
