"EXPECT: #(7 'constant ''owner'' of an Account read before it was set')"
"Spec §6.2, M14, conformance 88: a loader makes the object with basicNew and
 fills its constants by reflection. It fills one of the two; reading the
 filled one works, and reading the forgotten one through its accessor signals
 UnsetConstantError with M14's exact text instead of answering nil."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner

| a |
a := Account basicNew.
a instVarNamed: 'number' put: 7.
{a number. [a owner] on: UnsetConstantError do: [:e | e messageText]}
