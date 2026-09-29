"EXPECT: #(1 105 1)"
"A block temporary or parameter named like an instance variable is a distinct
 variable: assigning it leaves the instance variable alone, and an inner block
 that captures it reads the block's binding."
Object subclass: #G instanceVariableNames: 'a'.
G >> test
  | viaParam |
  a := 1.
  [:x | | a | a := x] value: 5.
  viaParam := ([:a | [a + 100]] value: 5) value.
  ^ { a. viaParam. a }

G new test
