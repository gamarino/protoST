"EXPECT: #(true 'DNU' 'DNU' 'DNU' 'DNU' false true false true false true false true)"
"§4.7, K14, D52: a class object is not an instance of itself. A send to a
 class looks the selector up on its metaclass chain (Foo class -> ... ->
 Object class -> Class -> Behavior -> Object) and never on the class's own
 instance side, as in Pharo:
   - QV defines an instance-side = that answers false, yet QV = QV is true
     (Object's identity, reached through Behavior);
   - an instance-side method sent to the class is not understood (QV greet,
     Array at: 1), and neither is a class-side constructor sent to an
     instance (#(1 2) new: 3, Set new new);
   - canUnderstand: describes the receiver's instances: Array's do not
     understand new:, the metaclass's (Array itself) do not understand at:put:;
   - respondsTo: follows the send."
Object subclass: #K14QV.
K14QV >> = other  ^ false.
K14QV >> greet  ^ 'hi-instance'.
| dnu |
dnu := [:block | block on: MessageNotUnderstood do: [:e | e return: 'DNU']].
{ K14QV = K14QV.
  dnu value: [K14QV greet].
  dnu value: [Array at: 1].
  dnu value: [#(1 2) new: 3].
  dnu value: [Set new new].
  Array class canUnderstand: #at:put:.
  Array class canUnderstand: #new:.
  Array canUnderstand: #new:.
  Array canUnderstand: #at:put:.
  (Array new: 2) respondsTo: #with:.
  Array respondsTo: #with:.
  K14QV new = K14QV new.
  (Set new add: K14QV; add: K14QV; yourself) size = 1 }
