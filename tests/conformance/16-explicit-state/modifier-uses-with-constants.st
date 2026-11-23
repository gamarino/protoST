"EXPECT: #(true true true #('id' 'tally' 'x'))"
"A textual class declaration with uses: and modifier clauses declares its
 variables once, with their modifiers (pre-flight ruling C8); the mixin's
 variable stays plain. (Symbols are compared, not printed: STATUS D35.)"
Object subclass: #Tagged instanceVariableNames: 'tag'.
Object subclass: #Holder
  uses: { Tagged }
  constantNames: 'id'
  atomicNames: 'tally'
  instanceVariableNames: 'x'.
{ (Holder instVarModifierOf: #id) == #constant.
  (Holder instVarModifierOf: #tally) == #atomic.
  (Holder instVarModifierOf: #tag) == #plain.
  Holder instVarNames }
