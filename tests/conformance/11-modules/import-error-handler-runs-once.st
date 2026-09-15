"EXPECT: 1 1 0 boom in module"
"D28 (§8.3, §11.2): an Error raised by an imported module's top level reaches"
"the importer's on:do: handler exactly once, with the original message text,"
"and the handler's value is the value of on:do:. Printed: module top-level"
"runs, handler runs, handler runs that saw another message text, result."
Import from: '_import_probe'.
result := [ Import from: '_raising_mod'. 'no error' ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText. e messageText ].
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', result) printNl.
