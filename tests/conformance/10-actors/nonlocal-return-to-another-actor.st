"EXPECT: #('BlockCannotReturn' true)"
"A ^ does not cross threads: a block with a ^ sent to another actor signals
 BlockCannotReturn there even while its home method is alive, waiting for the
 reply. The text used to say the home method had already returned; it now
 names both cases."
Object subclass: #Runner.
Runner >> run: aBlock  ^ aBlock value.

Object subclass: #Home.
Home >> go
  ^ ((Runner new asActor) run: [ ^ #fromTheBlock ]) wait.

| r |
r := [ Home new go ] on: Error do: [:e | e ].
{ r class name. (r messageText includesSubstring: 'another actor') }.
