"EXPECT: zd fooBar 3 set 42 Error 7 #()"
"ZeroDivide and MessageNotUnderstood are classes; exception sets; return:,
 return and description."
r1 := [1 / 0] on: ZeroDivide do: [:e | 'zd'].
r2 := [3 fooBar] on: MessageNotUnderstood do: [:e | e message selector].
r3 := [3 fooBar] on: MessageNotUnderstood do: [:e | e receiver].
r4 := [1 / 0] on: ZeroDivide, MessageNotUnderstood do: [:e | 'set'].
r5 := [Error signal: 'x'. 1] on: Error do: [:e | e return: 42].
r6 := [Error new signal] on: Error do: [:e | e description].
r7 := [(Error new signal) isNil ifTrue: [7] ifFalse: [8]] on: Error do: [:e | e return].
r8 := [3 fooBar] on: MessageNotUnderstood do: [:e | e message arguments].
r1 , ' ' , r2 , ' ' , r3 printString , ' ' , r4 , ' ' , r5 printString , ' ' , r6 , ' ' , (r7 isNil ifTrue: ['7'] ifFalse: ['bad']) , ' ' , r8 printString.
