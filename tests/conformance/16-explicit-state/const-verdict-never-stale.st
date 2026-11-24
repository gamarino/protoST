"EXPECT: #('built' 'M13 new' 'built' 'M13 new' 'built' 'built' 'M13 new' 'built' 'M13 new' 'built' 'M13 new' 'built' 'M13 x:y:' 'built' 'M13 new')"
"Spec §6.2: `new` and the kernel constructors ask each class once per
 instance-layout epoch whether it has constants, and keep the answer on the
 class. Every element below first builds an instance, so the class caches a
 verdict, then changes what the verdict must be and builds again. A verdict
 that survived any of these changes would build the second instance without
 the M13 check (or run the check on a class that no longer has constants):
 - an ancestor redeclared in place, gaining a constant and then losing it;
 - addBehavior: bringing a constant;
 - a uses: mixin redeclared in place, gaining a constant;
 - one of two mixins redeclared in place, gaining a constant and losing it;
 - a Point subclass (the x:y: kernel constructor) redeclared in place;
 - an OrderedCollection subclass (the built-in collection path)."
Object subclass: #Base instanceVariableNames: 'x'.
Base subclass: #Kid instanceVariableNames: 'k'.
Object subclass: #Plain instanceVariableNames: 'p'.
Object subclass: #Tag instanceVariableNames: '' constantNames: 'tag'.
Object subclass: #Mix instanceVariableNames: 'm'.
Object subclass: #User uses: {Mix}.
Object subclass: #Left instanceVariableNames: 'l'.
Object subclass: #Right instanceVariableNames: 'r'.
Object subclass: #Both uses: {Left. Right}.
Point subclass: #Point3 instanceVariableNames: 'z'.
OrderedCollection subclass: #Pile instanceVariableNames: 'top'.

| r try |
r := OrderedCollection new.
try := [:block :sel |
  [block value. 'built'] on: Error do: [:e |
    (e messageText includesSubstring: 'not set at the end of construction (' , sel , ')')
      ifTrue: ['M13 ' , sel]
      ifFalse: [e messageText]]].

r add: (try value: [Kid new] value: 'new').
Object subclass: #Base instanceVariableNames: 'x' constantNames: 'c'.
r add: (try value: [Kid new] value: 'new').
Object subclass: #Base instanceVariableNames: 'x'.
r add: (try value: [Kid new] value: 'new').

Plain new.
Plain addBehavior: Tag.
r add: (try value: [Plain new] value: 'new').

r add: (try value: [User new] value: 'new').
Object subclass: #Mix instanceVariableNames: 'm'.
r add: (try value: [User new] value: 'new').
Object subclass: #Mix instanceVariableNames: 'm' constantNames: 'mc'.
r add: (try value: [User new] value: 'new').

r add: (try value: [Both new] value: 'new').
Object subclass: #Right instanceVariableNames: 'r' constantNames: 'rc'.
r add: (try value: [Both new] value: 'new').
Object subclass: #Right instanceVariableNames: 'r'.
r add: (try value: [Both new] value: 'new').

Object subclass: #Left instanceVariableNames: 'l' writeOnceNames: 'w'.
Both new.
Object subclass: #Left instanceVariableNames: 'l' constantNames: 'lc' writeOnceNames: 'w'.
r add: (try value: [Both new] value: 'new').

r add: (try value: [Point3 x: 1 y: 2] value: 'x:y:').
Point subclass: #Point3 instanceVariableNames: 'z' constantNames: 'w'.
r add: (try value: [Point3 x: 1 y: 2] value: 'x:y:').

r add: (try value: [Pile new] value: 'new').
OrderedCollection subclass: #Pile instanceVariableNames: 'top' constantNames: 'bottom'.
r add: (try value: [Pile new] value: 'new').
r asArray printNl.
