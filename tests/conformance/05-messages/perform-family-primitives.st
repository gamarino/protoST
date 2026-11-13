"EXPECT: #(1 2 3 6 #('zork' 1) 'Error' 7 1 true)"
"perform:, perform:with:, perform:with:with: and perform:with:with:with: are primitives (0.10.0): methods with 0 to 3 arguments, doesNotUnderstand: with the whole Message, a wrong argument count, a primitive, a String selector, respondsTo:."
Object subclass: #Q.
Q >> a ^ 1.
Q >> b: x ^ x.
Q >> c: x d: y ^ x + y.
Q >> e: x f: y g: z ^ x + y + z.
Q >> doesNotUnderstand: m ^ { m selector. m arguments size }.
{ Q new perform: #a. Q new perform: #b: with: 2. Q new perform: #c:d: with: 1 with: 2.
  Q new perform: #e:f:g: with: 1 with: 2 with: 3. Q new perform: #zork with: 1.
  [ Q new perform: #b: ] on: Error do: [ :e | e class name ].
  3 perform: #+ with: 4. Q new perform: 'a'. (Q new respondsTo: #perform:with:) }
