"EXPECT: woof + generic / wag"
"§4: T3-c — a class given a behaviour at runtime keeps its own methods and
its inherited `super` path. `Dog` still answers `sound` (its own override,
calling `super sound` on `Animal`) AND gains `wag` from the `Wagging` mixin."
Object subclass: #Animal.
Animal >> sound  ^ 'generic'.
Animal subclass: #Dog.
Dog >> sound  ^ 'woof + ', super sound.
Object subclass: #Wagging.
Wagging >> wag  ^ 'wag'.
Dog addBehavior: Wagging.
^ (Dog newChild) sound, ' / ', (Dog newChild) wag.
