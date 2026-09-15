"EXPECT: 13"
"§6.4 + D29: ifTrue:ifFalse: and whileTrue: nested in and around to:do:,"
"each body evaluating non-inlined blocks."
n := 0.
1 to: 3 do: [ :i |
  i > 1
    ifTrue: [ #(1) do: [ :x | n := n + i ] ]
    ifFalse: [ [ n := n + 1 ] value ] ].
k := 0.
[ [ k < 3 ] value ] whileTrue: [
  1 to: 2 do: [ :j | [ n := n + 1 ] value ].
  k := k + 1 ].
n + 1.
