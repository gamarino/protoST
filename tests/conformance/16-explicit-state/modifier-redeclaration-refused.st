"EXPECT-ERROR: Account is already declared with 'balance' atomic; a class keeps the modifiers it was declared with"
"Spec §6.1 and M11, conformance 79: re-running a class declaration (as a REPL
 session does) that changes the modifier of an existing variable is refused,
 a plain redeclaration of a modified variable included."
Object subclass: #Account atomicNames: 'balance'.
Object subclass: #Account instanceVariableNames: 'balance'.
'not reached' printNl.
