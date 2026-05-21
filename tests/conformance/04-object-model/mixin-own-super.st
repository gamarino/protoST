"EXPECT: generic!"
"§4: T3-b — a mixin's own `super` keeps single-inheritance semantics. Loud
is a subclass of Animal; its `sound` calls `super sound` (reaching Animal).
Used as a mixin of Pet, Loud's `super` still resolves up Loud's OWN
definition chain, not the using class's parents."
Object subclass: #Animal.
Animal >> sound  ^ 'generic'.
Animal subclass: #Loud.
Loud >> sound  ^ (super sound), '!'.
Object subclass: #Pet uses: { Loud }.
Pet new sound.
