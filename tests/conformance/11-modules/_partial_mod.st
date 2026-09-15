"-- a module whose top level declares a class with methods and then raises —"
"-- used by import-error-partial-module.st --"
Object subclass: #PartialLoad.
PartialLoad >> greeting
  ^ 'declared before the module raised'.
PartialLoad >> tripled: aNumber
  ^ [ :x | x * 3 ] value: aNumber.
Error signal: 'boom in module'.
