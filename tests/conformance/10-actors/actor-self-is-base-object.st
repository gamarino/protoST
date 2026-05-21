"EXPECT: 20"
"§10.5: inside an actor method, a self-send is ordinary synchronous dispatch"
Object subclass: #Calc.
Calc >> twice: n
  ^ n + n.
Calc >> quadruple: n
  ^ self twice: (self twice: n).
a := Calc new asActor.
(a quadruple: 5) wait.
