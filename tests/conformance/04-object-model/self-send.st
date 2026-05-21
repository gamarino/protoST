"EXPECT: 36"
"§4.6: a self-send dispatches normally — quad: 9 = double(double(9)) = 36"
Object subclass: #Calc.
Calc >> double: n
  ^ n + n.
Calc >> quad: n
  ^ self double: (self double: n).
Calc new quad: 9.
