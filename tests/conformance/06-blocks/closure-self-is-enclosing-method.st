"EXPECT: 100"
"§6.3: self inside a block is the self of the enclosing method"
Object subclass: #Thing
  instanceVariableNames: 'v'.
Thing >> set: n
  v := n.
Thing >> v
  ^ v.
Thing >> readViaBlock
  ^ [ self v ] value.
t := Thing new.
t set: 100.
t readViaBlock.
