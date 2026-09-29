"EXPECT: caught caught caught"
"Integer, Float and floor division by zero all signal ZeroDivide."
a := [1 / 0] on: ZeroDivide do: [:e | 'caught'].
b := [1.5 / 0] on: ZeroDivide do: [:e | 'caught'].
c := [7 // 0] on: ZeroDivide do: [:e | 'caught'].
a , ' ' , b , ' ' , c.
