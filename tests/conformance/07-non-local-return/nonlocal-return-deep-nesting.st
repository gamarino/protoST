"EXPECT: 42"
"§7: a ^ targets the home method from arbitrary block-nesting depth"
Object subclass: #Deep.
Deep >> go
  [ [ [ ^ 42 ] value ] value ] value.
  ^ 0.
Deep new go.
