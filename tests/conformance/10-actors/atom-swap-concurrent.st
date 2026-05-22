"EXPECT: 1000"
"§10 Atom under contention: five actors each apply swap: 200 times to ONE
 shared Atom, running in parallel on the worker pool. swap: is a CAS-retry
 loop — a lost race re-reads the now-current value and retries — so no
 increment is ever lost. The final value is exactly 5 * 200 = 1000, proving
 the optimistic-concurrency path is correct under real parallel contention,
 with no lock anywhere."
counter := Atom on: 0.
Object subclass: #Bumper.
Bumper >> bumpInto: anAtom
  1 to: 200 do: [ :i | anAtom swap: [ :v | v + 1 ] ].
  ^ self.
a1 := Bumper new asActor.
a2 := Bumper new asActor.
a3 := Bumper new asActor.
a4 := Bumper new asActor.
a5 := Bumper new asActor.
f1 := a1 bumpInto: counter.
f2 := a2 bumpInto: counter.
f3 := a3 bumpInto: counter.
f4 := a4 bumpInto: counter.
f5 := a5 bumpInto: counter.
f1 wait. f2 wait. f3 wait. f4 wait. f5 wait.
counter value printNl.
