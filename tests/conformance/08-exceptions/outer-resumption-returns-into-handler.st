"EXPECT: inner continued"
"§8.4 / D7: when the enclosing handler resumes, ex outer answers the
 resumption value inside the inner handler, which continues; the protected
 block does not continue at the signal. The program prints 5 (from printNl)
 and answers 'inner continued', as in Pharo (Exception>>outer and
 Exception>>resumeUnchecked:). Before the fix, outer behaved as pass: nothing
 was printed and the answer was 'body'."
[[Warning signal: 'w'. 'body']
    on: Warning
    do: [:e | e outer printNl. 'inner continued']]
  on: Warning
  do: [:e | e resume: 5].
