"EXPECT: 1 1 0 0 boom in module"
"D28 (§11.2): module _nested_outer_mod imports _raising_mod, whose top level"
"raises. The Error crosses both module top levels and reaches the outermost"
"importer's handler once, with the original message text; neither top level"
"continues past the failing statement. Printed: _raising_mod top-level runs,"
"handler runs, handler runs with another message text, statements run after"
"the failing one, the value of on:do:."
Import from: '_import_probe'.
result := [ Import from: '_nested_outer_mod'. 'no error' ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText. e messageText ].
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', ImportProbe outerAfter printString, ' ',
  result) printNl.
