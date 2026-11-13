"EXPECT: #(true true true false true true false true true false true)"
"Review round 3 (R3-L2): a mixin is a parent class in protoST (LANGUAGE.md
 §4.11), and `on: aClass do:` selects an exception whose class has aClass
 among its ancestors, mixins included. isKindOf: and inheritsFrom: answer
 the same question: superclasses and `uses:` / addBehavior: mixins count,
 the class itself is not its own ancestor, and allSuperclasses stays the
 primary superclass chain."
Object subclass: #R3Tagged.
Error subclass: #R3MixErr uses: { R3Tagged }.
Object subclass: #R3Late.
Object subclass: #R3Plain.
R3Plain addBehavior: R3Late.
| e |
e := R3MixErr new.
{ e isKindOf: R3Tagged.
  e isKindOf: Error.
  R3MixErr inheritsFrom: R3Tagged.
  R3MixErr inheritsFrom: R3MixErr.
  [R3MixErr signal: 'x'. false] on: R3Tagged do: [:ex | ex return: true].
  R3Plain new isKindOf: R3Late.
  (R3MixErr allSuperclasses includes: R3Tagged).
  3 isKindOf: Integer.
  R3MixErr isKindOf: Class.
  e isKindOf: R3Plain.
  R3MixErr class inheritsFrom: Class }
