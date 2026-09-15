"EXPECT: 1 1 0 boom in module"
"D28 (§10, §11.2): an Error raised by a module's top level reaches the handler"
"of an import made inside an actor method (a worker thread) exactly once,"
"with the original message text. Printed: module top-level runs, handler"
"runs, handler runs with another message text, the value of on:do:."
Import from: '_import_probe'.
Object subclass: #ErrorImporter.
ErrorImporter >> import
  ^ [ Import from: '_raising_mod'. 'no error' ]
      on: Error
      do: [ :e | ImportProbe noteHandled: e messageText. e messageText ].
result := ErrorImporter new asActor import wait.
(ImportProbe runs printString, ' ', ImportProbe handled printString, ' ',
  ImportProbe wrong printString, ' ', result) printNl.
