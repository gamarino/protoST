"EXPECT: 121 104 x"
"§8.4: outer nests. Three handlers for one Warning:
   a: the innermost does outer, the middle does outer, the outermost resumes
      with 2; the middle's outer answers 2 and it resumes with 20; the
      innermost's outer answers 20 and it resumes with 21, so the signal
      answers 21 and the block 121.
   b: the middle does pass instead. Its handler search reaches the outermost,
      whose resume: 3 answers the innermost's pending outer (Pharo's
      Exception>>resumeUnchecked: returns to the most recent outer), so the
      innermost resumes with 4 and the block answers 104.
   c: the outermost returns: every handler inside is unwound."
| a b c |
a := [[[(Warning signal: 'w') + 100]
         on: Warning do: [:e | e resume: e outer + 1]]
        on: Warning do: [:e | e resume: e outer * 10]]
      on: Warning do: [:e | e resume: 2].
b := [[[(Warning signal: 'w') + 100]
         on: Warning do: [:e | e resume: e outer + 1]]
        on: Warning do: [:e | e pass]]
      on: Warning do: [:e | e resume: 3].
c := [[[(Warning signal: 'w') + 100]
         on: Warning do: [:e | e resume: e outer + 1]]
        on: Warning do: [:e | e resume: e outer * 10]]
      on: Warning do: [:e | e return: 'x'].
a printString , ' ' , b printString , ' ' , c
