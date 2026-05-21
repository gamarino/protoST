"EXPECT: M1"
"§4: T3-b — diamond resolution order. Both mixins define `tag`; the
selector is reachable via two parents. It resolves to the FIRST in the
documented resolution order: the `uses:` mixins are searched in listed
order, so M1 (listed first) wins over M2."
Object subclass: #M1.
M1 >> tag  ^ 'M1'.
Object subclass: #M2.
M2 >> tag  ^ 'M2'.
Object subclass: #D uses: { M1. M2 }.
D new tag.
