"32 actors x 50 msgs = 1600 msgs. NACTORS > NWORKERS but few — like current saturation_big."
Object subclass: #W instanceVariableNames: 'count'.
W >> initialize count := 0. ^ self.
W >> work
  | sum |
  sum := 0.
  1 to: 5000 do: [ :i | sum := sum + i ].
  count := count + 1.
  ^ sum.

actors := OrderedCollection new.
1 to: 32 do: [ :i | | a | a := W new. a initialize. actors add: (a asActor) ].

WorkerPool stopProcessing.
futures := OrderedCollection new.
1 to: 50 do: [ :m | actors do: [ :a | futures add: (a work) ] ].

t0 := Object new __monotonicMillis.
WorkerPool startProcessing.
result := 0.
futures do: [ :f | result := result + f wait ].
t1 := Object new __monotonicMillis.
('ELAPSED_MS ' , (t1 - t0) printString) printNl.
result = 20004000000 ifFalse: [Error signal: 'saturation_32a: wrong result ' , result printString].
Transcript showCr: 'VERIFIED ' , result printString.
