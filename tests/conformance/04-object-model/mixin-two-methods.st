"EXPECT: a Money: 100"
"§4: T3-b — a class assembled with two mixins via `uses:` understands a
method contributed by each mixin. `describe` comes from Printable,
`amount` is Money's own; both reachable on one instance."
Object subclass: #Printable.
Printable >> describe  ^ 'a ', self typeName.

Object subclass: #Amountable.
Amountable >> show  ^ self describe, ': ', self amountString.

Object subclass: #Money instanceVariableNames: 'cents' uses: { Printable. Amountable }.
Money >> setCents: c  cents := c. ^ self.
Money >> typeName  ^ 'Money'.
Money >> amountString  ^ '100'.

(Money new setCents: 100) show.
