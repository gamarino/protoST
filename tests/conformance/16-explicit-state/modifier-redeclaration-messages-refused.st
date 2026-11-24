"EXPECT: #('Acc is already declared with ''b'' atomic; a class keeps the modifiers it was declared with' 'Acc is already declared with ''b'' atomic; a class keeps the modifiers it was declared with' 'Acc is already declared with ''b'' atomic; a class keeps the modifiers it was declared with' true true true true)"
"Spec §6.1, M11 on the message forms (fix round 1 of Task 2): re-running
 subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames:,
 subclass:instanceVariableNames: or subclass:instanceVariableNames:uses: with a
 changed modifier is refused, and the refused declaration binds nothing: Acc
 is still the first class. A re-run that keeps the modifiers is accepted.
 (Symbols are compared, not printed: STATUS D35.)"
| first texts again |
(Object yourself) subclass: #Acc instanceVariableNames: 'h' constantNames: '' writeOnceNames: '' atomicNames: 'b'.
first := Acc.
texts := OrderedCollection new.
[ (Object yourself) subclass: #Acc instanceVariableNames: 'h' constantNames: 'b' writeOnceNames: '' atomicNames: '' ]
  on: Error do: [:e | texts add: e messageText ].
[ (Object yourself) subclass: #Acc instanceVariableNames: 'h b' ]
  on: Error do: [:e | texts add: e messageText ].
[ (Object yourself) subclass: #Acc instanceVariableNames: 'b' uses: { } ]
  on: Error do: [:e | texts add: e messageText ].
texts add: Acc == first.
texts add: (Acc instVarModifierOf: #b) == #atomic.
(Object yourself) subclass: #Acc instanceVariableNames: 'h n' constantNames: '' writeOnceNames: '' atomicNames: 'b'.
again := Acc.
texts add: again ~~ first.
texts add: (again instVarModifierOf: #b) == #atomic.
texts asArray
