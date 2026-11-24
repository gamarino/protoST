"EXPECT-ERROR: A is already declared with 'b' atomic; a class keeps the modifiers it was declared with"
"Spec §6.1, M11: a redeclaration under another superclass is still a
 redeclaration of the class named A, so it keeps the modifiers of the
 variables it repeats."
Object subclass: #Base instanceVariableNames: 'x'.
Object subclass: #A atomicNames: 'b'.
Base subclass: #A instanceVariableNames: 'b'.
'not reached' printNl.
