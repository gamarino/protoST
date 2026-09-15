"EXPECT: 2 1 0 42"
"D28 (§8.4, §11.2): retry in the importer's handler re-evaluates the protected"
"block, which imports the module again; the failed load was not cached, so"
"the top level runs a second time and, the failure condition now gone, the"
"import answers the module. Printed: module top-level runs, handler runs,"
"handler runs with another message text, a value computed by the module."
Import from: '_import_probe'.
ImportProbe failing: true.
m := [ Import from: '_flaky_mod' ]
  on: Error
  do: [ :e |
    ImportProbe noteHandled: e messageText.
    ImportProbe failing: false.
    e retry ].
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', m Recovered answer printString) printNl.
