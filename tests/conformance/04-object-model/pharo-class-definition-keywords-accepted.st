"EXPECT: #(3 3)"
"Class definitions copied from Pharo compile: package:, category: and
 poolDictionaries: are accepted and ignored."
Object subclass: #P1
  instanceVariableNames: 'x'
  classVariableNames: ''
  package: 'Demo'.
Object subclass: #P2
  instanceVariableNames: 'x'
  classVariableNames: ''
  poolDictionaries: ''
  category: 'Demo-Core'.
P1 >> x  ^ 3.
P2 >> x  ^ 3.

{ P1 new x. P2 new x }
