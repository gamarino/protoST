"EXPECT: #('new is generated for Account, which has constants: it runs super new and then checks them, and cannot be redefined; give your constructor another name (line 3)' 'new is generated for Sub, which has constants: it runs super new and then checks them, and cannot be redefined; give your constructor another name (line 3)' 'the constant ''number'' of an Account is not set at the end of construction (new)' 'the constant ''number'' of a Sub is not set at the end of construction (new)' 7)"
"Maintainer ruling of 2026-11-23, M9 for the generated new, the install-time
 path: a class built by the message form is not in the compiler's view, so
 __installClassMethod:as: refuses a user new on a class with constants, its
 own (Account) or inherited (Sub). Each refused method is in a helper module,
 so the refusal is caught here. Both classes keep the checking new, and a
 constructor under another name installs."
(Object yourself) subclass: #Account
  instanceVariableNames: ''
  constantNames: 'number'
  writeOnceNames: ''
  atomicNames: ''.
Account >> number
  ^ number
(Account yourself) subclass: #Sub
  instanceVariableNames: 's'
  constantNames: ''
  writeOnceNames: ''
  atomicNames: ''.
Account class >> numbered
  ^ self number: 7

| own sub |
own := [Import from: '_new_redefine_account'. 'installed'] on: Error do: [:e | e messageText].
sub := [Import from: '_new_redefine_sub'. 'installed'] on: Error do: [:e | e messageText].
{own. sub. [Account new] on: Error do: [:e | e messageText].
 [Sub new] on: Error do: [:e | e messageText]. Account numbered number}
