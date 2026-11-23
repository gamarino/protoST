"EXPECT: #('no signal' 'an Account' '  notes: nil' '  number: 7' '  owner: <unset constant>' '  oid: nil')"
"Plan decisions D-h and G4 (the debugger's row of spec §6.2): inspect is not
 a program read. It prints an unset constant as <unset constant> on the
 Transcript and signals nothing. A nil plain or write-once variable still
 prints nil. The Transcript is swapped for a recorder while inspect runs."
Object subclass: #Account instanceVariableNames: 'notes' constantNames: 'number owner'
  writeOnceNames: 'oid'.

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

| a saved recorder outcome |
a := Account basicNew.
a instVarNamed: #number put: 7.
saved := Transcript.
recorder := Recorder new.
Smalltalk at: #Transcript put: recorder.
outcome := [a inspect. 'no signal'] on: Error do: [:e | e messageText].
Smalltalk at: #Transcript put: saved.
{outcome} , recorder lines
