"EXPECT-ERROR: new is generated for Account, which has constants: it runs super new and then checks them, and cannot be redefined; give your constructor another name (line 9)"
"Spec §6.2, M9, conformance 58 as amended by the maintainer ruling of
 2026-11-23: a class with constants has a generated class-side new that runs
 super new and then the end-of-construction check. A user new on it would
 skip or reorder that check, so the compiler refuses one, even one that
 calls super new. The class gets its initialisation through initialize or a
 constructor under another name."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account class >> new
  ^ super new
Account >> initialize
  number := 1
'not reached' printNl.
