"EXPECT: #('h' #('cb start' 'handler continued'))"
"§10: a thenDo: callback belongs to whoever registered it, not to the code
 that settles the future. resolve: runs the callbacks at once, here inside a
 Warning handler that is itself inside an on: Error do:. The callback's
 Error is not caught by the settler's handler (its error is reported on
 stderr as a callback error, and the callback stops); the settler's handler
 continues and answers 'h'. Before the fix the settler's on: Error handler
 ran on the callback's error, and its return: was dropped."
| f log r |
log := OrderedCollection new.
f := Future new.
f thenDo: [:v | log add: 'cb start'. Error signal: 'from callback'. log add: 'cb end'].
r := [[Warning signal: 'w'. 'body']
        on: Warning
        do: [:e | f resolve: 1. log add: 'handler continued'. 'h']]
      on: Error
      do: [:e | log add: 'settler handler ran: ' , e messageText. e return: 'settler-ret'].
{r. log asArray}
