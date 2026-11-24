"EXPECT: #('__copyWithInstVarValues: expects 2 values, got 1' '__copyWithInstVarValues: expects an Array' '__copyWithInstVarValues: expects 2 values, got 1' 7)"
"Plan decision D-h (Task 8 review m-4): the private __copyWithInstVarValues:
 checks its argument against the runtime's list of the receiver's instance
 variables. An Array of the wrong size and an argument that is not an Array
 signal an Error and copy nothing. The same guard stops a deepCopy whose
 class overrides allInstVarNames with a list that is not the runtime's; the
 original keeps its value."
Object subclass: #Account instanceVariableNames: 'notes' constantNames: 'number'.
Account >> number
  ^ number

Account subclass: #Shortened instanceVariableNames: ''.
Shortened class >> allInstVarNames
  ^ #('number')

| a s caught |
caught := [:block | block on: Error do: [:e | e messageText]].
a := Account basicNew.
a instVarNamed: #number put: 7.
s := Shortened basicNew.
s instVarNamed: #number put: 7.
{caught value: [a __copyWithInstVarValues: #(1)].
 caught value: [a __copyWithInstVarValues: 3].
 caught value: [s deepCopy].
 s number}
