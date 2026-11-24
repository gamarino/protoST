"EXPECT: #('M13' 'unchecked' true #() 'user new' true 'M13' 'M13' true 'M13' true 'built' 'M13' 'built')"
"Maintainer ruling of 2026-11-23, staleness of the generated new, which
 follows the constructor's rules (const-generated-new-never-stale.st covers
 the subclasses and mixin users of a class redeclared in place, and
 tests/cli/test_cli_repl_generated_new_m9.sh the refusal of a declaration
 that would give a class with a user new constants).
 (1) Gone, redeclared in place (the same class object) without its
 constant, loses the generated new: Object class >> new answers the object
 unchecked, and Gone's metaclass holds nothing. (2) Removing a stale
 generated new never removes a user method: the user new defined once Gone
 has no constants survives a further redeclaration in place without
 constants. (3) addBehavior: of a behaviour with constants gives the class a
 checked new: metaclass lookup reaches Tagged's generated new before
 Object class >> new, so Plain needs no copy, and Tagged keeps its own.
 (4) A subclass without constants of its own inherits the generated new
 through metaclass lookup: its metaclass holds none. (5) A class whose
 metaclass lookup reaches a kernel new first (a SortedCollection subclass)
 and that uses a mixin redeclared in place: when the mixin gains a constant
 the class gets a generated new of its own over the kernel one, and loses
 it when the mixin drops the constant."
Object subclass: #Gone instanceVariableNames: 'h' constantNames: 'g'.
| r tag old |
r := OrderedCollection new.
tag := [:blk | [blk value. 'unchecked'] on: Error do: [:e |
    (e messageText includesSubstring: 'not set at the end of construction')
        ifTrue: ['M13'] ifFalse: [e messageText]]].
old := Gone.
r add: (tag value: [Gone new]).
Object subclass: #Gone instanceVariableNames: 'h'.
r add: (tag value: [Gone new]).
r add: old == Gone.
r add: Gone class selectors asArray.
Gone class >> new
  ^ 'user new'
Object subclass: #Gone instanceVariableNames: 'h' writeOnceNames: 'w'.
r add: Gone new.
r add: old == Gone.
Object subclass: #Tagged instanceVariableNames: '' constantNames: 'tg'.
Object subclass: #Plain instanceVariableNames: 'p'.
Plain addBehavior: Tagged.
r add: (tag value: [Plain new]).
r add: (tag value: [Tagged new]).
r add: (Plain class selectors includes: #new) not.
Tagged subclass: #Heir instanceVariableNames: 'e'.
r add: (tag value: [Heir new]).
r add: (Heir class selectors includes: #new) not.
Object subclass: #Mix instanceVariableNames: 'm'.
SortedCollection subclass: #Ranked uses: {Mix}.
r add: ([Ranked new. 'built'] on: Error do: [:e | e messageText]).
Object subclass: #Mix instanceVariableNames: 'm' constantNames: 'mc'.
r add: (tag value: [Ranked new]).
Object subclass: #Mix instanceVariableNames: 'm'.
r add: ([Ranked new. 'built'] on: Error do: [:e | e messageText]).
r asArray
