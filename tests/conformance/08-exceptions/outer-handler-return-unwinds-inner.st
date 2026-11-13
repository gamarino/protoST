"EXPECT: 7 false true"
"§8.4: when the enclosing handler returns (return:), ex outer does not answer:
 the unwind to the enclosing on:do: passes through the inner handler, whose
 ensure: block runs and whose remaining statements do not."
| after ens r |
after := false.
ens := false.
r := [[Warning signal: 'w'. 'body']
        on: Warning
        do: [:e | [e outer. after := true. 'inner'] ensure: [ens := true]]]
      on: Warning
      do: [:e | e return: 7].
r printString , ' ' , after printString , ' ' , ens printString
