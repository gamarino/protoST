"EXPECT: #(#('x' 'y') #('x' 'y' 'z') 3 7 true false true #('norm' 'z') 'A 3D point' 'Geometry' true false 1 5 true)"
"Reflection: declared and inherited instance variables, access by name and
 index, canUnderstand:, selectors, subclasses, class comments and
 categories, and deepCopy."
Object subclass: #P2 instanceVariableNames: 'x y'.
P2 subclass: #P3 instanceVariableNames: 'z'.
P2 >> setX: ax y: ay  x := ax. y := ay.
P3 >> z  ^ z.
P3 >> norm  ^ x + y + z.
P3 comment: 'A 3D point'.
P3 category: 'Geometry'.

| p q |
p := P3 new setX: 1 y: 2.
p instVarNamed: 'z' put: 4.
q := p deepCopy.
q instVarAt: 1 put: 5.
{ P2 instVarNames. P3 allInstVarNames. (p instVarAt: 3) - 1. p norm.
  (P3 canUnderstand: #norm). (P2 canUnderstand: #norm). (P3 canUnderstand: #setX:y:).
  P3 selectors. P3 comment. P3 category.
  (P2 subclasses includes: P3). (P3 subclasses includes: P2).
  p instVarAt: 1. q instVarAt: 1. (Smalltalk version beginsWith: 'protoST') }
