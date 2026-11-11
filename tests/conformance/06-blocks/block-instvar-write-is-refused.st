"EXPECT: #(true true true nil 7 true true)"
"A block closure is an immutable value (since 0.10.0). instVarNamed:put: on
 a block signals ModificationForbidden (an Error) naming the immutability instead of dropping the
 write silently: the handler runs, the block keeps no such variable and
 still evaluates. The same send on an ordinary instance still writes. A
 BlockClosure method that writes to self through instVarNamed:put: gets the
 same Error."
Object subclass: #Holder instanceVariableNames: 'x'.
BlockClosure >> tagWith: t self instVarNamed: 'tag' put: t.
| b refused text cls h refusedInMethod |
b := [ :a | a + 1 ].
refused := false.
text := ''.
[ b instVarNamed: 'foo' put: 3 ]
    on: Error do: [ :e | refused := true. text := e messageText. cls := e class ].
h := Holder new.
h instVarNamed: 'x' put: 7.
refusedInMethod := false.
[ b tagWith: #t ] on: Error do: [ :e | refusedInMethod := true ].
{ refused.
  cls == ModificationForbidden.
  (text includesSubstring: 'immutable').
  b instVarNamed: 'foo'.
  h instVarNamed: 'x'.
  (b value: 1) = 2.
  refusedInMethod }
