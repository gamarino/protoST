"EXPECT: done 2 0"
"§8.4: an enclosing handler that retries re-evaluates its protected block;
 ex outer does not answer and the inner handler does not continue (after
 stays 0). The second attempt does not signal."
| n after r |
n := 0.
after := 0.
r := [n := n + 1.
      [n = 1 ifTrue: [Warning signal: 'w']. 'done ' , n printString]
        on: Warning
        do: [:e | e outer. after := after + 1. 'inner']]
      on: Warning
      do: [:e | e retry].
r , ' ' , after printString
