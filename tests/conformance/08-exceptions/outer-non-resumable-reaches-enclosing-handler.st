"EXPECT: outer x false"
"§8.4: for a non-resumable exception ex outer behaves as ex pass: the
 enclosing handler runs and its return unwinds the inner handler."
| after r |
after := false.
r := [[Error signal: 'x']
        on: Error do: [:e | e outer. after := true. 'unreached']]
      on: Error do: [:e | 'outer ' , e messageText].
r , ' ' , after printString
