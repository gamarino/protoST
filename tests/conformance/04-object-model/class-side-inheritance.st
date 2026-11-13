"EXPECT: #('base-c' 'sub-c' 'base-i' 'base-c' 'sub-i' 'sub-c' 'base-i')"
"§4.7, K14: the class side is inherited along the class chain and a
 subclass overrides it like the instance side (K14IO, and K14IG below it).
 A class object finds a class-side method anywhere on its chain before an
 instance-side method of the same selector, as a Smalltalk metaclass chain
 does: K14IS defines an instance-side w, yet K14IS w answers its
 superclass's class-side w."
Object subclass: #K14IB.
K14IB >> w  ^ 'base-i'.
K14IB class >> w  ^ 'base-c'.
K14IB subclass: #K14IO.
K14IO class >> w  ^ 'sub-c'.
K14IB subclass: #K14IS.
K14IS >> w  ^ 'sub-i'.
K14IO subclass: #K14IG.
{ K14IB w. K14IO w. K14IO new w. K14IS w. K14IS new w. K14IG w. K14IG new w }
