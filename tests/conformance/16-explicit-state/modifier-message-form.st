"EXPECT: #(true true true #('h' 'n' 'b'))"
"Spec §6.1, conformance 81: the message form
 subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames:
 carries all four clauses; instVarModifierOf: answers #constant, #atomic and
 #plain. The receiver is an expression, so the statement is a real send and
 not the compiler's textual class declaration (pre-flight ruling C4). The
 answers are compared with the literal Symbols: a Symbol of six bytes or
 fewer prints as a String (STATUS D35), so #atomic printString is 'atomic'."
(Object yourself) subclass: #Acc
  instanceVariableNames: 'h'
  constantNames: 'n'
  writeOnceNames: ''
  atomicNames: 'b'.

{ (Acc instVarModifierOf: #n) == #constant.
  (Acc instVarModifierOf: #b) == #atomic.
  (Acc instVarModifierOf: #h) == #plain.
  Acc instVarNames }
