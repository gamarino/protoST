"EXPECT: 3 2 0 42"
"D28 (§11.2): a module whose top level raised is not cached, so importing it"
"again runs its top level again: twice failing (each failure handled once"
"with the original message), then succeeding once the failure condition is"
"gone. Printed: module top-level runs, handler runs, handler runs with"
"another message text, a value computed by the finally loaded module."
Import from: '_import_probe'.
ImportProbe failing: true.
[ Import from: '_flaky_mod' ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText ].
[ Import from: '_flaky_mod' ]
  on: Error
  do: [ :e | ImportProbe noteHandled: e messageText ].
ImportProbe failing: false.
m := Import from: '_flaky_mod'.
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', m Recovered answer printString) printNl.
