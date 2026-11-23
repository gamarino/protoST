"EXPECT: #(true true true true 'Sub has no instance variable ''zork''')"
"Spec §6.1 and §11: instVarModifierOf: answers the modifier of a declared
 instance variable, own or inherited (from a superclass or a mixin), and
 signals Error for a name the class does not declare. The receiver is a
 class, so the text names it bare (pre-flight ruling C7). Symbols are
 compared, not printed: a Symbol of six bytes or fewer prints as a String
 (STATUS D35)."
Object subclass: #Base atomicNames: 'balance' constantNames: 'number'.
Object subclass: #Marked writeOnceNames: 'oid'.
Base subclass: #Sub instanceVariableNames: 'x' uses: { Marked }.
{ (Sub instVarModifierOf: #balance) == #atomic.
  (Sub instVarModifierOf: 'number') == #constant.
  (Sub instVarModifierOf: #oid) == #writeOnce.
  (Sub instVarModifierOf: #x) == #plain.
  [ Sub instVarModifierOf: #zork ] on: Error do: [:e | e messageText ] }
