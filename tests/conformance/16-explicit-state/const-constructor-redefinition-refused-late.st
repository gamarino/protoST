"EXPECT: #('number:owner: is the generated constructor of Account and cannot be redefined; give your constructor another name (line 4)' 'number:owner:extra: is the generated constructor of Sub and cannot be redefined; give your constructor another name (line 4)' 7 'ok')"
"Spec §6.2, M9, the install-time path: a class built by the message form
 (an expression receiver, pre-flight ruling C4) is not in the compiler's
 view, so __installClassMethod:as: refuses the definition when it is
 installed, comparing the selector with the class's generated constructor
 (read through the chain, ruling C9). Each refused method is in a helper
 module, so the refusal is caught here. The generated constructor still
 works, and a constructor under another name installs."
(Object yourself) subclass: #Account
  instanceVariableNames: ''
  constantNames: 'number owner'
  writeOnceNames: ''
  atomicNames: ''.
Account >> number
  ^ number
(Account yourself) subclass: #Sub
  instanceVariableNames: ''
  constantNames: 'extra'
  writeOnceNames: ''
  atomicNames: ''.
Account class >> number: n
  ^ self number: n owner: 'default'

| own sub |
own := [Import from: '_ctor_redefine_account'. 'installed'] on: Error do: [:e | e messageText].
sub := [Import from: '_ctor_redefine_sub'. 'installed'] on: Error do: [:e | e messageText].
{own. sub. (Account number: 7 owner: 'ada') number. [(Account number: 1) number = 1 ifTrue: ['ok']] value}
