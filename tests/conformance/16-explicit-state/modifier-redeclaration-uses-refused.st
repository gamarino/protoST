"EXPECT-ERROR: H is already declared with 'tally' atomic; a class keeps the modifiers it was declared with"
"Spec §6.1, M11: re-running a textual declaration with uses: that changes the
 modifier of an existing variable is refused, as on the plain textual path
 (fix round 1 of Task 2). The uses: path builds a new class object each
 time, so the declaration it repeats is the class bound under the name."
Object subclass: #Tg instanceVariableNames: 'tag'.
Object subclass: #H uses: { Tg } atomicNames: 'tally'.
Object subclass: #H uses: { Tg } instanceVariableNames: 'tally'.
'not reached' printNl.
