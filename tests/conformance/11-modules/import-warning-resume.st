"EXPECT: 1 1 0 5 7"
"§8.4, §11.2: resume: in the importer's handler makes the Warning signalled by"
"a module's top level answer the value, and the top level continues to its"
"end, so the import answers the module. Printed: module top-level runs,"
"handler runs, handler runs with another message text, the value signal"
"answered inside the module, a value computed by the module."
Import from: '_import_probe'.
ImportProbe expect: 'careful'.
m := [ Import from: '_warning_mod' ]
  on: Warning
  do: [ :e | ImportProbe noteHandled: e messageText. e resume: 5 ].
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', ImportProbe resumed printString, ' ',
  m AfterWarning answer printString) printNl.
