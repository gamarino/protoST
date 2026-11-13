"EXPECT: #(true true true true true 7)"
"Review round 2 (R2-L4): perform: of a selector the receiver does not
 understand goes through doesNotUnderstand:, as a direct send does and as
 in Pharo: a user override answers (on either side of a class, with the
 performed arguments in the Message), and otherwise a resumable
 MessageNotUnderstood is signalled, whose resumption value perform: answers."
Object subclass: #R2G.
R2G >> doesNotUnderstand: aMessage  ^ aMessage selector.
R2G class >> doesNotUnderstand: aMessage  ^ ('c' , aMessage selector) asSymbol.
{ (R2G new perform: #foo) == #foo.
  (R2G perform: #bar) == #cbar.
  (R2G new perform: #a:b: with: 1 with: 2) == #a:b:.
  [3 perform: #zork] on: MessageNotUnderstood do: [:e | e return: e receiver = 3].
  [3 perform: #zork] on: MessageNotUnderstood do: [:e | e return: e message selector == #zork].
  [3 perform: #zork] on: MessageNotUnderstood do: [:e | e resume: 7] }
