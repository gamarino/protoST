"EXPECT: HEY!"
"§4: T3-c — on-the-fly behaviour composition. `addBehavior:` adds a mixin's
method to an existing class at runtime, with no recompilation. An instance
created AFTER the call responds to the mixin's method."
Object subclass: #Greeter.
Object subclass: #Loud.
Loud >> shout  ^ 'HEY!'.
Greeter addBehavior: Loud.
(Greeter newChild) shout.
