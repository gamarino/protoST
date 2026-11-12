"EXPECT: 5 inner continued false"
"§8.4 / D7: ex outer answers the enclosing handler's resumption value inside
 the inner handler; the protected block is not continued (its 'body' is never
 reached, so bodyRan stays false)."
| seen r bodyRan |
bodyRan := false.
r := [[Warning signal: 'w'. bodyRan := true. 'body']
        on: Warning
        do: [:e | seen := e outer. 'inner continued']]
      on: Warning
      do: [:e | e resume: 5].
seen printString , ' ' , r , ' ' , bodyRan printString
