"EXPECT: 1 1 0 1"
"D28 (§8.5, §11.2): an Error raised inside an ensure: block of a module's top"
"level is handled once by the importer, and the module's ensure: cleanup runs"
"once while the unwind passes through it. Printed: module top-level runs,"
"handler runs, handler runs with another message text, cleanup runs."
Import from: '_import_probe'.
[ Import from: '_raising_ensure_mod' ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText ].
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', ImportProbe cleanups printString) printNl.
