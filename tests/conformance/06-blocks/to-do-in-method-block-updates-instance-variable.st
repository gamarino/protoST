"EXPECT: 18"
"§6.3 + D29: inside a method, a do: block nested in a to:do: body updates an"
"instance variable: three passes over #(1 2 3)."
Object subclass: #Accumulator instanceVariableNames: 'acc'.
Accumulator >> run
  acc := 0.
  1 to: 3 do: [ :i | #(1 2 3) do: [ :x | acc := acc + x ] ].
  ^ acc.
Accumulator new run.
