"EXPECT: #('the constant ''number'' of a Kid is not set at the end of construction (new)' 'the constant ''extra'' of a Kid2 is not set at the end of construction (new)' 'the constant ''k'' of a Ranked is not set at the end of construction (new)' #(1 2) 'kid' 'the constant ''number'' of a Pharo is not set at the end of construction (new)')"
"Spec §6.2, conformance 58 extended to subclasses (round 3 of the maintainer
 ruling of 2026-11-23): a class-side new override written ^ super new on a
 subclass of a class with constants reaches the generated new, a hidden
 parent of the metaclass, and is checked. This holds for a subclass that
 only inherits the constants (Kid), one that adds its own (Kid2), and a
 SortedCollection subclass, whose kernel new still runs first (Ranked, a
 default sort block, which Ranked2, setting the constant, sorts with). An override whose initialize sets the constant answers
 the instance (Fills). A subclass with constants of its own under a class
 whose new skips super new (Pharo, under Loose) is still checked: its own
 generated new runs that new as its super new."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number'.
Account subclass: #Kid instanceVariableNames: 'k'.
Kid class >> new
  ^ super new
Account subclass: #Kid2 instanceVariableNames: '' constantNames: 'extra'.
Kid2 >> initialize
  number := 1
Kid2 class >> new
  ^ super new
SortedCollection subclass: #Ranked instanceVariableNames: '' constantNames: 'k'.
Ranked class >> new
  ^ super new
Ranked subclass: #Ranked2 instanceVariableNames: ''.
Ranked2 >> initialize
  k := 'set'
Object subclass: #Fills instanceVariableNames: '' constantNames: 'tag'.
Fills >> initialize
  tag := 'kid'
Fills >> tag
  ^ tag
Fills class >> new
  ^ super new
Object subclass: #Loose instanceVariableNames: 'l'.
Loose class >> new
  ^ self basicNew initialize
Loose subclass: #Pharo instanceVariableNames: '' constantNames: 'number'.

| r |
r := [:blk | [blk value] on: Error do: [:e | e messageText]].
{r value: [Kid new].
 r value: [Kid2 new].
 r value: [Ranked new].
 (Ranked2 new add: 2; add: 1; yourself) asArray.
 Fills new tag.
 r value: [Pharo new]}
