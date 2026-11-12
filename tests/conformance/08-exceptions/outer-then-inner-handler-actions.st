"EXPECT: 2 second 21 3"
"§8.4: after ex outer answers, the inner handler is still the active handler:
 return:, retry and resume: act on the inner on:do: and the original signal.
   - return: (e outer + 1): the inner on:do: answers 2;
   - retry: the inner protected block runs again and answers 'second';
   - resume: (e outer + 1): the signal answers 20 + 1, the block adds nothing;
   - on:do:on:do: (one construct, two clauses): outer skips the sibling
     clause and reaches the enclosing handler, which resumes with 3."
| n a b c d |
a := [[Warning signal: 'w'. 'body']
        on: Warning do: [:e | e return: e outer + 1]]
      on: Warning do: [:e | e resume: 1].
n := 0.
b := [[n := n + 1. n = 1 ifTrue: [Warning signal: 'w']. 'second']
        on: Warning do: [:e | e outer. e retry]]
      on: Warning do: [:e | e resume: 0].
c := [[Warning signal: 'w']
        on: Warning do: [:e | e resume: e outer + 1]]
      on: Warning do: [:e | e resume: 20].
d := [[Warning signal: 'w'. 'body']
        on: Warning do: [:e | e outer printString]
        on: Warning do: [:e | 'sibling clause']]
      on: Warning do: [:e | e resume: 3].
a printString , ' ' , b , ' ' , c printString , ' ' , d
