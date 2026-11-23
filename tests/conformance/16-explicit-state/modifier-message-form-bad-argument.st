"EXPECT: #(true 'subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames: expects a String for constantNames:, not 3' false)"
"The message form refuses a clause argument that is not a String (nil stands
 for ''), with an Error a handler catches by class, and declares nothing."
| r |
r := [ (Object yourself) subclass: #Bad instanceVariableNames: '' constantNames: 3 writeOnceNames: '' atomicNames: '' ]
  on: Error do: [:e | { e class == Error. e messageText } ].
{ r first. r last. Smalltalk includesKey: #Bad }
