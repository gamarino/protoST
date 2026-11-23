"EXPECT: #(true 1 'constant ''owner'' of an Account read before it was set')"
"Spec §6.2, M14, conformance 89: a class-side new written in the Pharo idiom
 `^ self basicNew initialize` skips the end-of-construction check, so an
 initialize that forgets owner answers the object without M13. The mistake
 surfaces at the first read of owner, as UnsetConstantError."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account class >> new
  ^ self basicNew initialize
Account >> initialize
  number := 1
Account >> number
  ^ number
Account >> owner
  ^ owner

| a |
a := Account new.
{a class == Account. a number. [a owner] on: UnsetConstantError do: [:e | e messageText]}
