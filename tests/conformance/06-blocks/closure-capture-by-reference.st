"EXPECT: 3"
"§6.3: capture is by reference — the method observes a block's mutation"
Object subclass: #Mut.
Mut >> run
  | c |
  c := 0.
  [ c := c + 1 ] value.
  [ c := c + 1 ] value.
  [ c := c + 1 ] value.
  ^ c.
Mut new run.
