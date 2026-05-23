"128 actors x 12 msgs = 1536 msgs. NACTORS >> NWORKERS — minimal per-actor contention.
 Each actor's mailbox is small, fewer popMailboxHead CAS per actor."
Object subclass: #W instanceVariableNames: 'count'.
W >> initialize count := 0. ^ self.
W >> work
  | sum |
  sum := 0.
  1 to: 5000 do: [ :i | sum := sum + i ].
  count := count + 1.
  ^ sum.

actors := OrderedCollection new.
1 to: 128 do: [ :i | | a | a := W new. a initialize. actors add: (a asActor) ].

WorkerPool stopProcessing.
futures := OrderedCollection new.
1 to: 12 do: [ :m | actors do: [ :a | futures add: (a work) ] ].

t0 := Object new __monotonicMillis.
WorkerPool startProcessing.
futures do: [ :f | f wait ].
t1 := Object new __monotonicMillis.
(t1 - t0) printNl. futures size.
