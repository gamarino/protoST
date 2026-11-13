"EXPECT: #(true true false true false false true true true true true true)"
"D52, review round 2 (R2-L1): Pharo's metaclass reflection. soleInstance and
 instanceClass answer a metaclass's class; isMeta is true for metaclasses
 only; lookupSelector: follows the lookup an instance's send does and
 answers the behavior holding the method (Pharo answers the CompiledMethod,
 whose methodClass this is), nil when nothing answers."
Object subclass: #R2A.
R2A subclass: #R2B.
R2A >> i  ^ 1.
R2A class >> c  ^ 2.
{ R2A class soleInstance == R2A.
  R2B class instanceClass == R2B.
  R2A isMeta.
  R2A class isMeta.
  Metaclass isMeta.
  3 class isMeta.
  (R2B lookupSelector: #i) == R2A.
  (R2B lookupSelector: #zork) isNil.
  (R2B class lookupSelector: #c) == R2A class.
  (R2B lookupSelector: #c) isNil.
  (R2B class lookupSelector: #superclass) == Behavior.
  (R2B lookupSelector: #printString) == Object }
