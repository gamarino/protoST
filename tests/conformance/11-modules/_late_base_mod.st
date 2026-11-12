"-- a module exporting a class with an instance variable, a class variable
and a class-instance variable -- used by the S37 tests: a method of the
importing program, compiled before this module runs, names them."
Object subclass: #LateBase
  instanceVariableNames: 'secret'
  classVariableNames: 'shared'.

LateBase class instanceVariableNames: 'made'.

LateBase >> init
  secret := 42.

LateBase >> shared
  ^ shared.

LateBase class >> noteMade
  made := (made ifNil: [ 0 ]) + 1.
  ^ made.
