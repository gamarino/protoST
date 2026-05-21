"XFAIL: EXPECT-ERROR"
"§10.3 / §10.7: an exception unhandled inside an actor method rejects that
message's Future, and `wait` is specified to 'block until settled; return the
value, or RE-RAISE the rejection'. NEW — undocumented discrepancy (not in §14):
waiting on a rejected Future does NOT re-raise — the program exits 0 and prints
the cause object instead of failing. See report."
Object subclass: #Faulty.
Faulty >> boom
  Error signal: 'actor failure'.
a := Faulty new asActor.
(a boom) wait.
