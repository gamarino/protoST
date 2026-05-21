"EXPECT: 1"
"§7: a non-local return abandons the trailing statements of the home method"
Object subclass: #Early.
Early >> go
  #(1) do: [ :x | ^ x ].
  ^ 999.
Early new go.
