"EXPECT: #(#('tag' 'third' 'base') #('tag' 'third') #('tag' 'key' 'own') true false #('kept') 'constant ''key'' of a Pair read before it was set' '  key: <unset constant>')"
"Task 6 concern 6 (Task 8 ruling): allInstVarNames lists the instance
 variables in the runtime's order, the superclass's, then each mixin's in
 listed order, then the class's own, without going through superclass, which
 answers the first mixin after uses: or addBehavior:. Before, the variables
 of a second mixin were missed (Both gave #('tag')). deepCopy, instVarAt: and
 inspect then see a constant held by a second mixin."
Object subclass: #Grown instanceVariableNames: 'base'.
Object subclass: #Tagged instanceVariableNames: 'tag'.
Object subclass: #Third instanceVariableNames: 'third'.
Object subclass: #Keyed instanceVariableNames: '' constantNames: 'key'.
Grown addBehavior: Tagged.
Grown addBehavior: Third.
Object subclass: #Both uses: { Tagged. Third }.
Object subclass: #Pair instanceVariableNames: 'own' uses: { Tagged. Keyed }.
Pair >> key
  ^ key

Object subclass: #Recorder instanceVariableNames: 'lines current'.
Recorder >> initialize
  lines := OrderedCollection new.
  current := ''
Recorder >> show: aString
  current := current , aString
Recorder >> print: anObject
  current := current , anObject printString
Recorder >> cr
  lines add: current.
  current := ''
Recorder >> lines
  ^ lines asArray

| p copy blank saved recorder |
p := Pair basicNew.
p instVarNamed: #key put: (OrderedCollection with: 'kept').
copy := p deepCopy.
blank := Pair basicNew.
saved := Transcript.
recorder := Recorder new.
Smalltalk at: #Transcript put: recorder.
blank inspect.
Smalltalk at: #Transcript put: saved.
{Grown allInstVarNames.
 Both allInstVarNames.
 Pair allInstVarNames.
 (p instVarAt: 2) == p key.
 copy key == p key.
 copy key asArray.
 [blank instVarAt: 2] on: UnsetConstantError do: [:e | e messageText].
 recorder lines at: 3}
