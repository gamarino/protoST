"EXPECT: #(true true true true true true true true 'K14MR class' 'K14MR class' true true false true #('c') #('i') 'Metaclass' 'Metaclass class')"
"§4.7, K14, D52: the metaclass chain as Pharo has it (without
 ClassDescription): Foo class class is Metaclass, Metaclass class class is
 Metaclass, Foo class superclass is its superclass's metaclass, Object class
 superclass is Class, Class superclass is Behavior, Behavior superclass is
 Object. A class is a kind of Class, a metaclass a kind of Metaclass, and
 selectors lists each side's own methods."
Object subclass: #K14MR.
K14MR subclass: #K14MS.
K14MR >> i  ^ 1.
K14MR class >> c  ^ 2.
{ K14MR class class == Metaclass.
  K14MR class class class == Metaclass class.
  Metaclass class class == Metaclass.
  K14MS class superclass == K14MR class.
  K14MR class superclass == Object class.
  Object class superclass == Class.
  Class superclass == Behavior.
  Behavior superclass == Object.
  K14MR class printString.
  K14MR class name.
  K14MR isKindOf: Class.
  K14MR class isKindOf: Metaclass.
  K14MR isKindOf: K14MR.
  K14MR isKindOf: Object.
  K14MR class selectors.
  K14MR selectors.
  K14MR class class printString.
  Metaclass class printString }
