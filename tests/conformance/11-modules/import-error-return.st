"EXPECT: 1 1 0 42"
"D28 (§8.4, §11.2): return: in the importer's handler abandons the module's"
"top level and makes the importer's on:do: answer the value, handling the"
"module's Error once. Printed: module top-level runs, handler runs, handler"
"runs with another message text, the value of on:do:."
Import from: '_import_probe'.
result := [ Import from: '_raising_mod'. 'no error' ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText. e return: 42 ].
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', result printString) printNl.
