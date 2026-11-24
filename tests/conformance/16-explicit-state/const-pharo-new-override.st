"EXPECT: #('new is generated for Account, which has constants: it runs super new and then checks them, and cannot be redefined; give your constructor another name (line 3)' true 1 'constant ''owner'' of an Account read before it was set' 'the constant ''owner'' of an Account is not set at the end of construction (new)')"
"Spec §6.2, M9 and M14, conformance 89 as amended by the maintainer ruling
 of 2026-11-23: a class-side new in the Pharo idiom, ^ self basicNew
 initialize, can no longer replace the generated new of a class with
 constants, so it cannot skip the end-of-construction check: installed late
 (from a helper module, so the refusal is caught here), it is refused. The
 same mistake made by hand, basicNew initialize, still answers the object
 without M13, and it surfaces at the first read of owner as
 UnsetConstantError. new itself refuses it with M13."
(Object yourself) subclass: #Account
  instanceVariableNames: ''
  constantNames: 'number owner'
  writeOnceNames: ''
  atomicNames: ''.
Account >> initialize
  number := 1
Account >> number
  ^ number
Account >> owner
  ^ owner

| refused a |
refused := [Import from: '_new_redefine_account'. 'installed'] on: Error do: [:e | e messageText].
a := Account basicNew initialize.
{refused. a class == Account. a number. [a owner] on: UnsetConstantError do: [:e | e messageText].
 [Account new. 'no error'] on: Error do: [:e | e messageText]}
