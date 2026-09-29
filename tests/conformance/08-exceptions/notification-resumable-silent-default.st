"EXPECT: nil nil 'hi' 6 true true false"
"A Notification is a resumable Exception; unhandled, it answers nil and prints nothing (the runner requires a clean exit); handled, it can be resumed with a value."
a := Notification signal: 'nobody listens'.
b := Notification new signal.
c := [Notification signal: 'hi'] on: Notification do: [:e | e messageText].
d := [(Notification signal: 'x') + 1] on: Notification do: [:e | e resume: 5].
t := a printString , ' ' , b printString , ' ' , c printString , ' ' , d printString , ' ' , Notification new isResumable printString , ' ' , (Notification inheritsFrom: Exception) printString , ' ' , (Notification inheritsFrom: Error) printString.
t.
