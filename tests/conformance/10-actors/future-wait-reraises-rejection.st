"EXPECT-ERROR"
"§10.3 / §10.7: an exception unhandled inside an actor method rejects that
message's Future, and `wait` is specified to 'block until settled; return the
value, or RE-RAISE the rejection'. Waiting on a rejected Future re-raises, so
this program exits non-zero.

Note: per §3.4 a method body with no `^` runs until the next top-level form;
`boom` therefore ends with an explicit `^ self` so the lines below it are read
as top-level code rather than swallowed into the method body."
Object subclass: #Faulty.
Faulty >> boom
  Error signal: 'actor failure'.
  ^ self.
a := Faulty new asActor.
(a boom) wait.
