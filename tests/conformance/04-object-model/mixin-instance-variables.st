"EXPECT: box"
"§4: T3-b — a class with its OWN instance variables AND mixins. A mixin
may itself declare instance variables; an instance of the using class
reads and writes the mixin's ivar. `subclass:instanceVariableNames:uses:`
declares both the class's own ivar and pulls in the mixin."
Object subclass: #HasName instanceVariableNames: 'theName'.
HasName >> name: n  theName := n. ^ self.
HasName >> name  ^ theName.

Object subclass: #Widget instanceVariableNames: 'kind' uses: { HasName }.
Widget >> kind: k  kind := k. ^ self.
Widget >> kind  ^ kind.

((Widget new name: 'box') kind: 'ui') name.
