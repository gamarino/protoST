"EXPECT: sub+base"
"§4: T3-b — `super` from a method of a multiply-inheriting class starts
at the next class in the resolution order after the defining class. The
primary superclass subtree is searched before the `uses:` mixins, so a
`greet` defined on BOTH the primary superclass Base and the mixin MixA
resolves to Base's via `super`."
Object subclass: #Base.
Base >> greet  ^ 'base'.

Object subclass: #MixA.
MixA >> greet  ^ 'mixA'.

Base subclass: #Sub uses: { MixA }.
Sub >> greet  ^ 'sub+', super greet.

Sub new greet.
