"EXPECT: #(7 12 5)"
"A call-form send needs its ( right after the name, as a keyword needs its
 colon. A method ending in `^ y` followed, after a blank line, by a statement
 that starts with ( used to take that statement as the arguments of y(...),
 inside the method, with no error; and a unary method whose body starts with
 ( on the next line was read as a call-form declaration."
| out |
Object subclass: #Cell
  instanceVariableNames: 'y'

Cell >> y
  ^ y

(out := OrderedCollection new) add: 3 + 4.

Cell >> area
  (y := 3 * 4).
  ^ y

Cell >> sum(a, b)
  ^ a + b

out add: Cell new area.
out add: (Cell new sum(2, 3)).
out asArray.
