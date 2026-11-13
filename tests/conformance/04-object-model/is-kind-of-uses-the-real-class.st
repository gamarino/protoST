"EXPECT: #(true true false true false true)"
"isKindOf: tests the receiver's real class and its ancestors, superclasses and
 mixins alike, as Pharo's (whose `self class` is a bytecode that a `class`
 method does not override). Since 0.10.0 it is a primitive; a class whose
 `class` method answers another class is still a kind of its real class."
Object subclass: #Animal.
Animal subclass: #Dog.
Dog >> class ^ String.
Object subclass: #Swim.
Animal subclass: #Duck uses: { Swim }.
{ Dog new isKindOf: Animal. Dog new isKindOf: Dog. Dog new isKindOf: String.
  Duck new isKindOf: Swim. 3 isKindOf: String. 3 isKindOf: Integer }
