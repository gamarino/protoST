"EXPECT: #('installed' 'installed' true 'constant ''number'' of an Account read before it was set' 'the constant ''number'' of a Sub is not set at the end of construction (new)' 7)"
"Spec §6.2 (conformance 58 and 89), the install-time path, round 3 of the
 maintainer ruling of 2026-11-23: a class built by the message form is not
 in the compiler's view, so its class-side methods install late. A user new
 on a class with constants installs: in the Pharo idiom (Account) it skips
 the end-of-construction check, so the first read of the unset constant
 signals UnsetConstantError (M14); one that calls super new (Sub, whose
 constants are inherited from Base) reaches the generated new and is checked
 (M13). (A subclass of Account would reach Account's own new instead: the
 user's override governs what its subclasses inherit.)
 A constructor under another name still installs."
(Object yourself) subclass: #Account
  instanceVariableNames: ''
  constantNames: 'number'
  writeOnceNames: ''
  atomicNames: ''.
Account >> number
  ^ number
(Object yourself) subclass: #Base
  instanceVariableNames: ''
  constantNames: 'number'
  writeOnceNames: ''
  atomicNames: ''.
(Base yourself) subclass: #Sub
  instanceVariableNames: 's'
  constantNames: ''
  writeOnceNames: ''
  atomicNames: ''.
Account class >> numbered
  ^ self number: 7

| own sub a |
own := [Import from: '_new_redefine_account'. 'installed'] on: Error do: [:e | e messageText].
sub := [Import from: '_new_redefine_sub'. 'installed'] on: Error do: [:e | e messageText].
a := Account new.
{own. sub. a class == Account. [a number] on: UnsetConstantError do: [:e | e messageText].
 [Sub new] on: Error do: [:e | e messageText]. Account numbered number}
