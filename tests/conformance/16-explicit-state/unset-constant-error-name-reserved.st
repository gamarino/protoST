"EXPECT: #('UnsetConstantError is a reserved kernel class name; a class cannot be declared under it' 'UnsetConstantError is a reserved kernel class name; a class cannot be declared under it' 'UnsetConstantError is a reserved kernel class name; a class cannot be declared under it' 'UnsetConstantError is a reserved kernel class name; a class cannot be declared under it' 'UnsetConstantError is a reserved kernel class name; a class cannot be declared under it' 'UnsetConstantError is a reserved kernel class name; a class cannot be declared under it' true true true)"
"Spec §6.9: since 0.17.0 the name UnsetConstantError is reserved in the
 global namespace. Every declaration path that would rebind it is refused
 with an Error signalled in place, and the global stays the kernel class: the
 textual declaration (in a helper module, so the refusal is caught here),
 subclass:, subclass:instanceVariableNames:, the uses: forms and the message
 form with modifiers."
Object subclass: #Account instanceVariableNames: '' constantNames: 'owner'.
Account >> owner
  ^ owner
Object subclass: #Mixin.

| refused kernel |
kernel := UnsetConstantError.
refused := [:block | [block value. 'declared'] on: Error do: [:e | e messageText]].
{refused value: [Import from: '_reserved_textual_decl'].
 refused value: [Object subclass: #UnsetConstantError].
 refused value: [Object subclass: #UnsetConstantError instanceVariableNames: 'x'].
 refused value: [Object subclass: #UnsetConstantError uses: {Mixin}].
 refused value: [Object subclass: #UnsetConstantError instanceVariableNames: 'x' uses: {Mixin}].
 refused value: [(Object yourself) subclass: #UnsetConstantError
                   instanceVariableNames: '' constantNames: 'x' writeOnceNames: '' atomicNames: ''].
 UnsetConstantError == kernel.
 UnsetConstantError superclass == Error.
 ([Account basicNew owner] on: UnsetConstantError do: [:e | e class]) == kernel}
