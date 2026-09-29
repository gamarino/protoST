"EXPECT: 6 6 2 caught"
"Future whenAll: / whenAny: and the & / | forms."
Object subclass: #Calc.
Calc >> twice: n
  ^ n * 2.

Calc >> fail
  ^ self error: 'boom'.

a := Calc new asActor.
f1 := a twice: 1.
f2 := a twice: 2.
all := Future whenAll: (Array with: f1 with: f2).
both := (f1 & f2) wait.
any := (Future whenAny: (Array with: f1 with: f2)) wait.
bad := [(Future whenAll: (Array with: (a twice: 3) with: a fail)) wait. 'no'] on: Error do: [:e | 'caught'].
((all wait) inject: 0 into: [:s :x | s + x]) printString , ' ' , (both inject: 0 into: [:s :x | s + x]) printString , ' ' , ((any = 2) | (any = 4) ifTrue: ['2'] ifFalse: ['bad']) , ' ' , bad.
