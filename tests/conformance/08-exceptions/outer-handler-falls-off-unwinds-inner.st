"EXPECT: 9 false"
"§8.4: an enclosing handler that falls off its end returns its value from its
 on:do:, so ex outer does not answer and the inner handler does not continue."
| after r |
after := false.
r := [[Warning signal: 'w'. 'body']
        on: Warning
        do: [:e | e outer. after := true. 'inner']]
      on: Warning
      do: [:e | 9].
r printString , ' ' , after printString
