"EXPECT: 5|2|IllegalResumeAttempt"
"§8.6: redefining Exception>>isResumable replaces the kernel method, so
 resume: sends the new one for every exception that inherits it (the
 kernel method is the only one resume: may skip sending). The redefinition
 counts its calls, then refuses a Warning tagged 'no'."
| a b |
Exception >> isResumable
  Smalltalk at: #ResumableCalls put: (Smalltalk at: #ResumableCalls ifAbsent: [0]) + 1.
  ^ self messageText ~= 'no'.
a := [(Warning signal: 'yes') + 0] on: Warning do: [:e | e resume: 5].
b := [[(Warning signal: 'no') + 0] on: Warning do: [:e | e resume: 6]]
       on: IllegalResumeAttempt do: [:e | e class name].
a printString , '|' , (Smalltalk at: #ResumableCalls) printString , '|' , b
