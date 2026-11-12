"A thenDo: callback that signals an Error, run by a resolve: inside a Warning
 handler that is inside an on: Error do:. The settler's handler must not run
 on the callback's error; the error is reported as a callback error."
| f log r |
log := OrderedCollection new.
f := Future new.
f thenDo: [:v | log add: 'cb start'. Error signal: 'from callback'. log add: 'cb end'].
r := [[Warning signal: 'w'. 'body'] on: Warning do: [:e | f resolve: 1. log add: 'handler continued'. 'h']]
       on: Error do: [:e | log add: 'settler handler ran: ', e messageText. e return: 'settler-ret'].
{r. log asArray} printString displayNl.
'done'
