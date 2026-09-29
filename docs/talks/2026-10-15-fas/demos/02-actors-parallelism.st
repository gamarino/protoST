"Demo 2 - the same computation on one thread and on twelve actors.
 Each actor runs on the worker pool (native threads, no global lock);
 the results must agree, the times are measured."

Object subclass: #PrimeCounter.

PrimeCounter >> countFrom: low to: high
  | n |
  n := 0.
  low to: high do: [:i | i isPrime ifTrue: [n := n + 1]].
  ^ n

| ranges serial parallel ms futures |
ranges := (0 to: 11) collect: [:k | (k * 4000 + 1) to: (k + 1) * 4000].

ms := Time millisecondsToRun: [
  serial := ranges inject: 0 into: [:sum :r |
    sum + (PrimeCounter new countFrom: r first to: r last)]].
Transcript showCr: 'one thread: ' , serial printString , ' primes'.
Transcript showCr: 'time one thread: ' , ms printString , ' ms'.

ms := Time millisecondsToRun: [
  futures := ranges collect: [:r | PrimeCounter new asActor countFrom: r first to: r last].
  parallel := futures inject: 0 into: [:sum :f | sum + f wait]].
Transcript showCr: '12 actors:  ' , parallel printString , ' primes'.
Transcript showCr: 'time 12 actors: ' , ms printString , ' ms'.

Transcript showCr: (serial = parallel) printString.
