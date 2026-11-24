"EXPECT: #('the constant ''extra'' of a Sub is not set at the end of construction (number:owner:)' true 2 'a' 1)"
"Spec §6.2, M13: a subclass that adds a constant gets a constructor over
 every constant, inherited first (number:owner:extra:). The superclass's
 constructor still reaches the subclass, and its check names the constant
 it cannot set and its own selector."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner
Account subclass: #Sub instanceVariableNames: '' constantNames: 'extra'.
Sub >> extra
  ^ extra

| s |
s := Sub number: 1 owner: 'a' extra: 2.
{[Sub number: 1 owner: 'a'] on: Error do: [:e | e messageText]. s class == Sub. s extra. s owner. s number}
