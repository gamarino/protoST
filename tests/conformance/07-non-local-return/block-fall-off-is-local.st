"EXPECT: 8"
"§7: a block falling off its end returns its last value locally, not from the method"
Object subclass: #Local.
Local >> go
  | r |
  r := [ 8 ] value.
  ^ r.
Local new go.
