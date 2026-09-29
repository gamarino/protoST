"EXPECT: #(4 4 4 4 0)"
"A class variable is one variable shared by the declaring class, its
 subclasses and all their instances: an instance-side method may assign it
 (Count := Count + 1 in initialize), and a write from a subclass or its class
 side updates the same storage instead of shadowing it."
Object subclass: #Base
  instanceVariableNames: ''
  classVariableNames: 'Count'.
Base subclass: #Sub.
Base class >> reset  Count := 0.
Base class >> count  ^ Count.
Base >> initialize  Count := (Count ifNil: [0]) + 1.
Base >> count  ^ Count.
Sub class >> bump  Count := Count + 1.

| b |
Base reset.
b := Base new.
Sub new.
Sub new.
Sub bump.
{ Base count. b count. Sub count. Sub new count - 1. (Base reset; count) }
