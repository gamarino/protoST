"-- a module exporting a Counter class — used by import-from.st --"
Object subclass: #Counter
  instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.
