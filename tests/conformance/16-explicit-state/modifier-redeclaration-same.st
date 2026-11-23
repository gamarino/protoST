"EXPECT: #(true true true true #('balance' 'number' 'note'))"
"Spec §6.1, M11: re-running a class declaration that keeps every modifier is
 accepted, and the class is the same object; a new variable may be added.
 (Symbols are compared, not printed: STATUS D35.)"
| first |
Object subclass: #Account atomicNames: 'balance' constantNames: 'number'.
first := Account.
Object subclass: #Account atomicNames: 'balance' constantNames: 'number' instanceVariableNames: 'note'.
{ first == Account.
  (Account instVarModifierOf: #balance) == #atomic.
  (Account instVarModifierOf: #number) == #constant.
  (Account instVarModifierOf: #note) == #plain.
  Account instVarNames }
