"EXPECT: #('UnsetConstantError is a reserved kernel class name; the global cannot be rebound' 'UnsetConstantError is a reserved kernel class name; the global cannot be rebound' 'UnsetConstantError is a reserved kernel class name; the global cannot be rebound' 'UnsetConstantError is a reserved kernel class name; the global cannot be rebound' 'UnsetConstantError is a reserved kernel class name; the global cannot be rebound' true true true true true)"
"Spec §6.9: the name UnsetConstantError is reserved in the global namespace,
 on every route that would rebind it, not only the declarations
 (unset-constant-error-name-reserved.st): Smalltalk at:put:, the primitive
 behind it, addBehavior: and addParent: on the kernel class, and addBehavior:
 on a copy that carries its name. Each is refused with an Error signalled in
 place, and nothing is rebound or rebuilt. The runtime signals M14 through
 the class it captured when the kernel loaded, so `on: UnsetConstantError
 do:` catches it afterwards, with the kernel class. (The REPL's top-level
 assignment is covered by cli_unset_constant_error_reserved.)"
Object subclass: #Account instanceVariableNames: '' constantNames: 'owner'.
Account >> owner
  ^ owner
Object subclass: #Mixin.

| kernel refused |
kernel := UnsetConstantError.
refused := [:block | [block value. 'rebound'] on: Error do: [:e | e messageText]].
{refused value: [Smalltalk at: #UnsetConstantError put: 42].
 refused value: [nil __globalAt: #UnsetConstantError put: 42].
 refused value: [UnsetConstantError addBehavior: Mixin].
 refused value: [UnsetConstantError addParent: Mixin].
 refused value: [UnsetConstantError copy addBehavior: Mixin].
 (Smalltalk at: #UnsetConstantError) == kernel.
 UnsetConstantError == kernel.
 (UnsetConstantError new isKindOf: Mixin) not.
 ([Account basicNew owner] on: UnsetConstantError do: [:e | e class]) == kernel.
 [[Account basicNew owner. false] on: kernel do: [:e | e variableName = 'owner']]
   on: Error do: [:e | false]}
