"XFAIL: EXPECT: #(true true true)"
"Spec §6.9: the name UnsetConstantError is reserved in the global namespace.
 The declaration paths refuse it (unset-constant-error-name-reserved.st), but
 the global dictionary does not: today `Smalltalk at: #UnsetConstantError
 put: 42` rebinds the global, M14 then falls back to a plain Error (the
 runtime finds the class by its global name), and a handler holding the
 kernel class no longer catches it. The program answers #(false false false).
 Recorded as STATUS D64; the same holds for ModificationForbidden and every
 other kernel class name, which are not reserved at all."
Object subclass: #Account instanceVariableNames: '' constantNames: 'owner'.
Account >> owner
  ^ owner

| kernel refused |
kernel := UnsetConstantError.
refused := [Smalltalk at: #UnsetConstantError put: 42. false] on: Error do: [:e | true].
{refused.
 (Smalltalk at: #UnsetConstantError) == kernel.
 [[Account basicNew owner. false] on: kernel do: [:e | true]]
   on: Error do: [:e | false]}
