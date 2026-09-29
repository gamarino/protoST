"EXPECT: true"
"An exception signalled without text answers its class name as messageText
 (it used to answer an unrelated integer); signal: text is kept."
m1 := [Error signal] on: Error do: [:e | e messageText].
m2 := [Error new signal: 'boom'] on: Error do: [:e | e messageText].
m3 := [Error new signal] on: Error do: [:e | e messageText].
(m1 = 'Error') & (m2 = 'boom') & (m3 = 'Error').
