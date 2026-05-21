"-- a module exporting a Counter class — used by the T3-a extensible-class
conformance tests. The class carries a `super`-using method (`bump`) so the
tests can observe a module method's own `super` resolving correctly once the
class is imported (the defining class is not a global of the importer)."
Object subclass: #Counter
  instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

Counter >> incrementBy: n
  value := value + n.

Object subclass: #BaseLabel
  instanceVariableNames: ''.

BaseLabel >> label
  ^ 'base'.

BaseLabel subclass: #ModuleLabel
  instanceVariableNames: ''.

ModuleLabel >> label
  "Calls super — when ModuleLabel is imported, neither it nor BaseLabel is a
  global of the importing program. `super` must still resolve by walking the
  receiver's prototype chain by identity."
  ^ super label.
