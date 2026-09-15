"EXPECT: boom in module / declared before the module raised / 42"
"D28 (§11.2): a module's top level declares a class, then raises. The load"
"fails and the importer handles the error, but the class was already declared"
"as a global and stays usable: its methods (and the block inside one) run"
"from the failed module's compiled code, which the runtime must keep alive."
"The allocations between the import and the sends reuse any freed memory."
msg := nil.
[ Import from: '_partial_mod' ]
  on: Error
  do: [ :e | msg := e messageText ].
junk := OrderedCollection new.
1 to: 2000 do: [ :i | junk add: (Array new: 50) ].
(msg, ' / ', PartialLoad new greeting, ' / ',
  (PartialLoad new tripled: 14) printString) printNl.
