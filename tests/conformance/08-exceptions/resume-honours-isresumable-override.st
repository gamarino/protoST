"EXPECT: false|IllegalResumeAttempt: cannot resume a non-resumable exception|aborted true IllegalResumeAttempt|illegal"
"§8.4 / §8.6: resumability is what the exception's isResumable answers, so a
 class that overrides it with ^ false (the ANSI / Pharo way to declare a
 non-resumable exception) is not resumable. resume: of such an exception
 signals IllegalResumeAttempt inside the handler that tried it, as Pharo's
 Exception>>resume: does; IllegalResumeAttempt is an Exception, not an Error.
   1. isResumable answers false;
   2. resume: in the handler signals IllegalResumeAttempt, caught outside;
   3. through outer (protoBaseST's newTransactionDo: pattern), the enclosing
      handler's resume: signals IllegalResumeAttempt, which unwinds through
      the inner handler: outer does not answer and the flag stays set;
   4. resume: of an Error signals IllegalResumeAttempt, which an on: Error
      handler does not take.
 Before the fix the override was ignored: the resume: succeeded."
Exception subclass: #NonRes.
NonRes >> isResumable
  ^ false.

| a b c d inFlight |
a := [NonRes signal: 'n'] on: NonRes do: [:e | e isResumable].
b := [[NonRes signal: 'n'. 'continued'] on: NonRes do: [:e | e resume: 1]]
       on: IllegalResumeAttempt do: [:e | e class name , ': ' , e messageText].
inFlight := false.
c := [[[NonRes signal: 'n'. 'committed']
         on: Exception
         do: [:e | | r |
           inFlight := true.
           r := e outer.
           inFlight := false.
           e resume: r]]
        on: NonRes do: [:e | e resume: 11]]
      on: IllegalResumeAttempt do: [:e | 'aborted'].
c := c , ' ' , inFlight printString , ' ' , (c = 'aborted' ifTrue: ['IllegalResumeAttempt'] ifFalse: ['-']).
d := [[[Error signal: 'x'] on: Error do: [:e | e resume: 1]]
        on: Error do: [:e | 'error handler']]
      on: IllegalResumeAttempt do: [:e | 'illegal'].
a printString , '|' , b , '|' , c , '|' , d
