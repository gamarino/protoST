"EXPECT: #(#('count' 'limit') #() #('a') #('count' 'limit') 2 1)"
"D52, review round 2 (R2-L1): `Foo class instanceVariableNames:` is recorded
 on the metaclass, so `Foo class instVarNames` lists the class-instance
 variables, as in Pharo; a subclass's metaclass declares none of its own but
 inherits them (allInstVarNames), and each class keeps its own values."
Object subclass: #R2C instanceVariableNames: 'a'.
R2C class instanceVariableNames: 'count limit'.
R2C subclass: #R2D.
R2C class >> bump  count := (count ifNil: [0]) + 1. ^ count.
R2C bump. R2C bump. R2D bump.
{ R2C class instVarNames. R2D class instVarNames. R2C instVarNames.
  R2D class allInstVarNames. R2C bump - 1. R2D bump - 1 }
