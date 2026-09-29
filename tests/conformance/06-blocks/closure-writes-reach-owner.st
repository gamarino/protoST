"EXPECT: 3 13"
"A block writing a variable of an enclosing activation updates that binding,
 and the enclosing activation sees it."
Object subclass: #T.
T >> run
  | total |
  total := 0.
  #(1 2) do: [:x | | tmp | tmp := x. total := total + tmp].
  ^ total.

T >> nested
  | total |
  total := 10.
  #(1) do: [:x | #(1 2) do: [:y | total := total + y]].
  ^ total.

T new run printString , ' ' , T new nested printString.
