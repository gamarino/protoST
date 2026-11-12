"EXPECT: nil inner|nil continued|nil"
"§8.4: with no enclosing handler, ex outer runs the exception's default action
 and, for a resumable exception, answers its value inside the inner handler
 (Pharo: UndefinedObject>>handleSignal: sends resumeUnchecked: with the
 defaultAction's value, which returns from outer). An unhandled Warning
 prints its text on stderr and its default action answers nil (§8.2); a
 Notification's answers nil silently. ex pass, by contrast, resumes the
 signal with that value."
| a b c |
a := [Warning signal: 'w'. 'body']
       on: Warning do: [:e | e outer printString , ' inner'].
b := [Notification signal: 'n'. 'body']
       on: Notification do: [:e | e outer printString , ' continued'].
c := [(Warning signal: 'w') printString]
       on: Warning do: [:e | e pass. 'unreached'].
a , '|' , b , '|' , c
