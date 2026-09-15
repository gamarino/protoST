"EXPECT: 1 1 1 0 outer"
"D28 (§8.4, §11.2): pass in the importer's inner handler hands a module's"
"Error to the enclosing handler; each handler runs once and the outer one"
"sees the original message text. Printed: module top-level runs, inner"
"handler runs, outer handler runs, outer handler runs with another message"
"text, the value of the outer on:do:."
Import from: '_import_probe'.
result := [
    [ Import from: '_raising_mod'. 'no error' ]
      on: Error
      do: [ :e | ImportProbe notePass. e pass ] ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText. 'outer' ].
(ImportProbe runs printString, ' ', ImportProbe passes printString, ' ',
  ImportProbe handled printString, ' ', ImportProbe wrong printString, ' ',
  result) printNl.
