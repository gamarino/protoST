"EXPECT: #(true true false 'Base default' 'Sub default')"
"Class-instance variables (Foo class instanceVariableNames: '...') are slots
 of each class object: the class-side singleton idiom works, and a subclass
 has its own slot rather than its superclass's value."
Object subclass: #Base instanceVariableNames: 'label'.
Base class instanceVariableNames: 'default'.
Base class >> default  ^ default ifNil: [default := self new setLabel: self name , ' default'].
Base >> setLabel: aString  label := aString.
Base >> label  ^ label.
Base subclass: #Sub.

{ Base default == Base default. Sub default == Sub default.
  Base default == Sub default. Base default label. Sub default label }
