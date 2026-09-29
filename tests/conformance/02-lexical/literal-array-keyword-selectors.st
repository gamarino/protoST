"EXPECT: #(1 #bar:baz: 3 2 true true 'abcdefghijkl' true)"
"Inside a literal array, keyword parts written without spaces form one
 symbol; a long Symbol's asString answers an equal String."
{ #(bar:baz:) size. #(bar:baz:) first. #(foo: #+ bar:baz:) size.
  #(at: put:) size. #(at: put:) first = #at:. #(at: put:) last = #put:.
  #abcdefghijkl asString. #abcdefghijkl asString isSymbol not }
