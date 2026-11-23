"EXPECT: #(true true false)"
"Spec §6.9, conformance 94: UnsetConstantError is a kernel class, a direct
 subclass of Error, bound at start-up. Reading an unset constant signals an
 instance of it, and like every Error it is not resumable."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> owner
  ^ owner

{UnsetConstantError superclass == Error.
 ([Account basicNew owner] on: UnsetConstantError do: [:e | e class]) == UnsetConstantError.
 ([Account basicNew owner] on: UnsetConstantError do: [:e | e isResumable])}
