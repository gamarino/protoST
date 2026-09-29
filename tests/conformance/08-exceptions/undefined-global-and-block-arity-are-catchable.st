"EXPECT: #('undefined global: Zork' 'block arg count mismatch (expected 2, got 1)')"
"A reference to an undefined global and a block called with the wrong number
 of arguments signal ordinary, catchable Errors."
{ [Zork new] on: Error do: [:e | e messageText].
  [[:a :b | a] value: 1] on: Error do: [:e | e messageText] }
