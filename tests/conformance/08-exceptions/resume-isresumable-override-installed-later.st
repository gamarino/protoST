"EXPECT: 7|IllegalResumeAttempt|9"
"§8.6: resume: reads the class marker without a send only while the
 exception's isResumable is the kernel's own Exception>>isResumable; the
 method is looked up on every resume:, so nothing is remembered about a
 class.
   1. a Late resumes (kernel isResumable: marker read directly);
   2. Late >> isResumable ^ false is installed afterwards: the next resume:
      sends it and signals IllegalResumeAttempt;
   3. a Warning, which still inherits the kernel method, resumes."
Warning subclass: #Late.

a := [(Late signal: 'x') + 0] on: Late do: [:e | e resume: 7].
Late >> isResumable
  ^ false.
b := [[(Late signal: 'x') + 0] on: Late do: [:e | e resume: 8]]
       on: IllegalResumeAttempt do: [:e | e class name].
c := [(Warning signal: 'w') + 0] on: Warning do: [:e | e resume: 9].
a printString , '|' , b , '|' , c printString
