"EXPECT: 'boom' 'Error' 'ZeroDivide' 'Notification' 'quiet' false true true"
"description answers the messageText, or the class name when none was given; an Error is not resumable, an Exception and a Warning are."
t1 := ([Error signal: 'boom'] on: Error do: [:e | e description]) printString , ' ' , ([Error signal] on: Error do: [:e | e description]) printString , ' ' , ([1 / 0] on: ZeroDivide do: [:e | e description]) printString.
t2 := ([Notification signal] on: Notification do: [:e | e description]) printString , ' ' , ([Notification signal: 'quiet'] on: Notification do: [:e | e description]) printString.
t3 := Error new isResumable printString , ' ' , Exception new isResumable printString , ' ' , Warning new isResumable printString.
t1 , ' ' , t2 , ' ' , t3.
